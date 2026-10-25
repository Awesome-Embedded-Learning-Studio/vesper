// The compositor eyeball lane: the whole C series on one screen. The desk
// is a Wayland client of whatever host carries it (WSLg here, measured
// 2560x1440 at scale 1, so the desk draws itself as a 1280x800 window),
// and a Wayland server for its own guests at the same time. Run it, then
// point a guest at the printed socket -- wl_block beside this binary
// bounces on the desk, and X apps ride the bridge once Xwayland is
// installed. The guests' windows are desk windows: draggable by the band,
// closable by the box, one keyboard between them.
//
// The old desk's own furniture rides along: the icon column on the left
// (double-click WELCOME for the about box, TERMINAL for the real terminal
// program birthed through the fourth slot), and one keyboard between
// everything -- a guest face, the terminal's face, whatever the pointer
// touched last. The host window's own close ends the run.
#include "compositor/guest_spawn.hpp"
#include "compositor/server.hpp"
#include "compositor/xwayland.hpp"

#include "fonts/title_font.hpp"
#include "fonts/ui_font.hpp"
#include "host/keymap.hpp"
#include "icons/desktop_icons.hpp"
#include "ui/desktop/desktop.hpp"
#include "ui/icon/icon.hpp"
#include "ui/shell_desk/shell_desk.hpp"
#include "ui/style/style.hpp"
#include "ui/style/theme.hpp"
#include "ui/window_frame/window_frame.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/display.hpp"
#include "visor/bindings/host.hpp"
#include "visor/bindings/posix/spawn.hpp"
#include "visor/bindings/wayland/wayland_window_host.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/pixel_traits.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/run_loop.hpp"
#include "visor/ui/text.hpp"
#include "visor/ui/widgets/widget.hpp"

#include <poll.h>
#include <unistd.h>

#include <sys/types.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <print>
#include <string_view>
#include <utility>

namespace {

using vesper::compositor::GuestSpawn;
using vesper::compositor::Server;
using vesper::desktop::Desktop;
using vesper::desktop::Icon;
using vesper::desktop::ShellDesk;
using vesper::desktop::WindowFrame;
using vesper::desktop::style::kCaptionGap;
using vesper::desktop::style::kIconSlotHeight;
using vesper::desktop::style::kIconSlotWidth;
using vesper::desktop::style::kPanelHeight;
using vesper::examples::kTermBodyArt;
using vesper::examples::kTermGoldArt;
using vesper::examples::kWindowContentArt;
using vesper::examples::kWindowFrameArt;
using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
using visor::canvas::ColorRole;

// Measured against the host (WSLg's rdp output): 2560x1440 at scale 1, so
// a half-screen desk window at 1280x800 sits comfortably without scaling.
constexpr SizeI kScreen{.w = 1280, .h = 800};
constexpr RectI kScreenBounds{.pos = PointI{0, 0}, .size = kScreen};
// The panel keeps the bottom strip: the local windows drag inside the work
// area, so a window can never park on top of the desk's own chrome.
constexpr RectI kWorkArea{
    .pos = PointI{0, 0},
    .size = SizeI{.w = kScreen.w, .h = kScreen.h - kPanelHeight},
};
using Frame = visor::canvas::Canvas<kScreen, visor::canvas::Xrgb8888Traits>;

// One quiet tick: the poll's ceiling, and the presentation beat the
// guests' frame rates follow.
constexpr int kTickMs = 15;

/// One column of three lines: a heading, a rule and two body lines in the
/// smaller face. A plain Widget because the frame adopts exactly one content
/// widget and this content is static.
class WelcomePanel final : public visor::ui::Widget {
  public:
    WelcomePanel(RectI bounds, const visor::draw::MonoFont& heading,
                 const visor::draw::MonoFont& body) noexcept
        : Widget{bounds}, heading_{heading}, body_{body} {}

  protected:
    void on_draw(const visor::ui::Painter& painter) const noexcept override {
        const PointI origin = bounds().pos;
        visor::ui::draw_text(painter, origin, heading_, "Welcome", ColorRole::Foreground, 1);
        const int rule_top = origin.y + 17;
        painter.fill(RectI{.pos = PointI{origin.x, rule_top}, .size = SizeI{.w = 120, .h = 1}},
                     ColorRole::Border);
        constexpr std::array<const char*, 3> lines{
            "A compositor desk on Wayland.",
            "Double-click an icon to open.",
            "Guest windows land from the socket.",
        };
        int y = rule_top + 7;
        for (const char* line : lines) {
            visor::ui::draw_text(painter, PointI{origin.x, y}, body_, line, ColorRole::Foreground,
                                 1);
            y += static_cast<int>(body_.line_height) + 2;
        }
    }

  private:
    visor::draw::MonoFont heading_;
    visor::draw::MonoFont body_;
};

/// What an icon's double click opens: the window, the band it shares the
/// desk's ring with, and the desk whose guests must step aside while a
/// local window holds the eye.
struct Opener {
    WindowFrame* win = nullptr;
    WindowFrame* other = nullptr;            // the band-mate: exactly one local band lit
    ShellDesk* desk = nullptr;               // whose guests yield the bands and the keys
    visor::ui::Widget* take_focus = nullptr; // the keyboard's first seat
};

/// The local ring: one band lit among the local windows, and the guests
/// stepped aside (their bands dark, the seat's keyboard word sent). The
/// tree's focus is not moved here -- the opener seats it right after.
void light_local_band(const Opener& opener, WindowFrame& self) noexcept {
    opener.desk->release_focus();
    self.set_active(true);
    if (opener.other != nullptr) {
        opener.other->set_active(false);
    }
}

/// The frame's press cue for a local window's band.
// NOLINTNEXTLINE(misc-const-correctness): the handler type is visor's void*.
void on_local_focus(WindowFrame& self, void* context) noexcept {
    light_local_band(*static_cast<const Opener*>(context), self);
}

/// The open reaction: showing the window, lifting it over its siblings and
/// over the guests, seating the keyboard where the window wants it.
// NOLINTNEXTLINE(misc-const-correctness): the handler type is visor's void*.
void open_window(Icon& /*self*/, void* context) noexcept {
    const auto* const opener = static_cast<const Opener*>(context);
    opener->win->set_visible(true);
    static_cast<void>(opener->win->raise());
    light_local_band(*opener, *opener->win);
    if (opener->take_focus != nullptr) {
        opener->take_focus->set_focused(true);
    }
}

/// The desk's selection: one icon wears the ring at a time.
struct Selection {
    Icon* current = nullptr;
};

void select_icon(Icon& self, void* context) noexcept {
    Selection& selection = *static_cast<Selection*>(context);
    if (selection.current == &self) {
        return;
    }
    if (selection.current != nullptr) {
        selection.current->set_selected(false);
    }
    selection.current = &self;
    self.set_selected(true);
}

/// The terminal program's binary: the environment's word first (the same
/// door VESPER_XWAYLAND opens), the running example's own directory second
/// (the build tree keeps the program a sibling of its desk).
const char* terminal_binary() noexcept {
    const char* named = std::getenv("VESPER_TERMINAL");
    if (named != nullptr && named[0] != '\0') {
        return named;
    }
    static std::array<char, 256> sibling{};
    if (sibling[0] == '\0') {
        const ssize_t self = ::readlink("/proc/self/exe", sibling.data(), sibling.size() - 1);
        if (self <= 0) {
            return nullptr;
        }
        sibling[static_cast<std::size_t>(self)] = '\0';
        char* slash = std::strrchr(sibling.data(), '/');
        if (slash == nullptr) {
            return nullptr;
        }
        ++slash;
        constexpr std::string_view k_program_name = "vesper_terminal";
        if (sibling.size() - static_cast<std::size_t>(slash - sibling.data()) <
            k_program_name.size() + 1) {
            return nullptr;
        }
        std::memcpy(slash, k_program_name.data(), k_program_name.size() + 1);
    }
    if (::access(sibling.data(), X_OK) != 0) {
        return nullptr;
    }
    return sibling.data();
}

/// What the TERMINAL icon needs to birth the program: the server that
/// adopts the connection, the table whose fourth slot does the birth, and
/// the spawn that owns the child.
struct TerminalOpener {
    Server* server = nullptr;
    const visor::host::HostTable* birth = nullptr;
    GuestSpawn* guest = nullptr;
};

/// The TERMINAL icon's reaction: birth the program when it is not already
/// on the desk. Its window lands through the shell like every guest's, and
/// a second click while it lives is quietly nobody's business.
// NOLINTNEXTLINE(misc-const-correctness): the handler type is visor's void*.
void open_terminal(Icon& /*self*/, void* context) noexcept {
    const auto* const opener = static_cast<const TerminalOpener*>(context);
    if (opener->guest->client() != nullptr || opener->guest->alive()) {
        return;
    }
    const char* binary = terminal_binary();
    if (binary == nullptr) {
        std::println(stderr, "vesper-compositor: no terminal program (set VESPER_TERMINAL)");
        return;
    }
    const std::array<const char*, 1> argv{binary};
    if (!opener->guest->launch(*opener->server, *opener->birth, binary, argv)) {
        std::println(stderr, "vesper-compositor: the terminal program refused the birth");
    }
}

} // namespace

int main() {
    auto opened = visor::host::wayland::WaylandWindowHost::open(nullptr, kScreen.w, kScreen.h, 1,
                                                                visor::host::PixelFormat::XRGB8888);
    if (!opened.has_value()) {
        std::println(stderr,
                     "vesper-compositor: cannot open the display (is WAYLAND_DISPLAY set?)");
        return 1;
    }
    auto host = std::move(*opened);

    Server server;
    if (!server.valid()) {
        std::println(stderr, "vesper-compositor: the display refused to come up");
        return 1;
    }
    // The bridge's Xwayland needs an output before its X screen stands up,
    // and the honest extent is the desk's own canvas.
    server.note_output_extent(kScreen.w, kScreen.h);
    const char* socket = server.publish_socket();
    if (socket == nullptr) {
        std::println(stderr, "vesper-compositor: no socket (XDG_RUNTIME_DIR unset?)");
        return 1;
    }

    static Frame frame; // four megabytes of canvas: storage, not stack
    visor::ui::InputRouter input{};
    visor::ui::DirtyRegions dirty{};
    Desktop desktop{kScreenBounds, "VESPER", vesper::fonts::title::kTitleFont,
                    vesper::desktop::style::kColorDeskTheme};
    ShellDesk desk{desktop, server, vesper::fonts::title::kTitleFont};
    desktop.set_dirty_sink(&dirty);
    dirty.push(kScreenBounds);

    // The X bridge, when the machine carries the binary: X apps become
    // desk windows through the same shell every other guest walks.
    // The desk's own table births the bridge: the fourth slot carries the
    // wiring the wayland host itself never speaks.
    visor::host::HostTable birth = host.table();
    birth.spawn = &visor::host::spawn_via_posix;
    vesper::compositor::XWayland bridge;
    if (bridge.launch(server, birth)) {
        desk.set_xwayland(&bridge);
        std::println("vesper-compositor: X bridge on DISPLAY={}", bridge.display());
    } else {
        std::println("vesper-compositor: no X bridge (install xorg-xwayland to arm it)");
    }

    // The terminal program (T3): born through the fourth slot when the
    // icon asks, its window arriving through the shell like every guest's.
    // Declared after the bridge so everything it outlives dies before it.
    GuestSpawn terminal;

    WelcomePanel welcome{RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 210, .h = 87}},
                         vesper::fonts::title::kTitleFont, vesper::fonts::ui::kUiFont};
    WindowFrame welcome_win{RectI{.pos = PointI{150, 60}, .size = SizeI{.w = 230, .h = 110}},
                            "About", vesper::fonts::title::kTitleFont, kWorkArea};

    // The icon column on the desk's left, one slot under the other. The
    // layered art: gold frame with ink glass for the about box, muted body
    // with a gold prompt for the terminal -- both layers one centered cell.
    constexpr std::array<Icon::Layer, 2> welcome_layers = [] {
        std::array<Icon::Layer, 2> layers{};
        layers[0] = {.art = &kWindowFrameArt, .ink = ColorRole::Accent};
        layers[1] = {.art = &kWindowContentArt, .ink = ColorRole::Foreground};
        return layers;
    }();
    constexpr std::array<Icon::Layer, 2> term_layers = [] {
        std::array<Icon::Layer, 2> layers{};
        layers[0] = {.art = &kTermBodyArt, .ink = ColorRole::Muted};
        layers[1] = {.art = &kTermGoldArt, .ink = ColorRole::Accent};
        return layers;
    }();
    const RectI welcome_slot{
        .pos = PointI{16, 16},
        .size =
            SizeI{
                .w = kIconSlotWidth,
                .h = kIconSlotHeight,
            },
    };
    const RectI term_slot{
        .pos = PointI{16, 16 + kIconSlotHeight + kCaptionGap},
        .size =
            SizeI{
                .w = kIconSlotWidth,
                .h = kIconSlotHeight,
            },
    };
    Icon welcome_icon{welcome_slot, welcome_layers, "WELCOME", vesper::fonts::ui::kUiFont};
    Icon term_icon{term_slot, term_layers, "TERMINAL", vesper::fonts::ui::kUiFont};

    // The openers: the about window keeps its band, the terminal icon
    // births the program (a guest window makes its own band).
    Opener welcome_opener{.win = &welcome_win, .other = nullptr, .desk = &desk};
    welcome_win.set_on_focus(&on_local_focus, &welcome_opener);
    TerminalOpener terminal_opener{.server = &server, .birth = &birth, .guest = &terminal};

    Selection selection;
    welcome_icon.set_on_activate(&open_window, &welcome_opener);
    term_icon.set_on_activate(&open_terminal, &terminal_opener);
    welcome_icon.set_on_select(&select_icon, &selection);
    term_icon.set_on_select(&select_icon, &selection);

    // Icons first, windows after: later children paint on top, so an open
    // window never hides behind an icon it covers.
    if (!desktop.add_child(&welcome_icon) || !desktop.add_child(&term_icon) ||
        !welcome_win.initialize() || !welcome_win.set_content(&welcome) ||
        !desktop.add_child(&welcome_win)) {
        std::println(stderr, "vesper-compositor: the desktop's child table is full");
        return 1;
    }
    // The desk starts with the about window hidden; the icon opens it, and
    // the terminal arrives only when its icon births the program.
    welcome_win.close();

    std::println("vesper-compositor: published {}", socket);
    // Redirected stdout is buffered; the socket's name is the one thing
    // the surrounding hands read.
    static_cast<void>(::fflush(stdout));
    std::println("vesper-compositor: try  WAYLAND_DISPLAY={} wl_block", socket);
    std::println("vesper-compositor: double-click an icon on the desk's left to open a local "
                 "window");
    if (bridge.display()[0] != '\0') {
        std::println("vesper-compositor: and  DISPLAY={} xhello", bridge.display());
    }

    // The wayland host speaks raw evdev numbers, the desk's consumers (the
    // terminal's byte table, the tree's named keys) eat code points: the
    // table crosses the fold before the desk sees a word. The seat side is
    // covered at the canvas, which folds the text back to the wire's evdev
    // numbers for the guests.
    vesper::host::TextFoldTable folded{host.table()};
    const visor::host::HostTable table = folded.table();
    while (!host.close_requested()) {
        // The sleeper set: the server's own fd wakes the desk when a guest
        // speaks, the tick wakes it for the presentation beat otherwise.
        struct pollfd waiter{
            .fd = server.event_fd(),
            .events = POLLIN,
            .revents = 0,
        };
        static_cast<void>(::poll(&waiter, 1, kTickMs));

        // One desk round: serve the guests, render the round's events and
        // debts, then present on this tick. The terminal's program pumps
        // itself inside its own process now.
        desk.pump();
        static_cast<void>(visor::ui::run_dirty(table, desktop, input, frame, kScreen, dirty));
        desk.settle_focus();
        desk.collect();
        static_cast<void>(visor::ui::run_dirty(table, desktop, input, frame, kScreen, dirty));
        server.note_time_ms(static_cast<std::uint32_t>(table.now_ms()));
        desk.present();
    }
    return 0;
}
