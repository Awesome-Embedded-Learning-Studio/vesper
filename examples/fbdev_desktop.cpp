// The fbdev lane: the same desk the X11 eyeball lane shows, on a bare Linux
// framebuffer with evdev input -- no display server, no compositor, one mmap
// and two device nodes. Run under the VM (scripts/vm/smoke.sh packs it into
// the initramfs) or by hand on a machine with a real /dev/fb0:
//   vesper-fbdev /dev/fb0 /dev/input/event3 /dev/input/event4
// where the nodes are the AT keyboard and the usb-tablet in either order
// fixed by the arguments. Assembly shape borrowed from Cinux-GUI@0b0c135
// host/linux_fbdev_main.cpp (2026-10-20): open devices, fill the table,
// run the loop -- the table filled is visor's, not the old repo's ABI. The guest's /init finds both
// by name; a run ends by SIGTERM from the surrounding `timeout`, there is no close button a bare
// framebuffer could own.
#include "desktop/desktop.hpp"
#include "desktop/icon.hpp"
#include "desktop/pty_source.hpp"
#include "desktop/style.hpp"
#include "desktop/terminal.hpp"
#include "desktop/window_frame.hpp"
#include "desktop_icons.hpp"
#include "fonts/term_font.hpp"
#include "fonts/title_font.hpp"
#include "fonts/ui_font.hpp"
#include "host/evdev_input.hpp"
#include "host/fbdev_device.hpp"
#include "host/pty.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/cinux/frame_expand.hpp"
#include "visor/bindings/display.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/host.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/run_loop.hpp"
#include "visor/ui/text.hpp"
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/label/label.hpp"
#include "visor/ui/widgets/widget.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <print>
#include <variant>

#include <poll.h>

namespace {

using vesper::desktop::Desktop;
using vesper::desktop::Icon;
using vesper::desktop::PtySource;
using vesper::desktop::Terminal;
using vesper::desktop::WindowFrame;
using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
using visor::canvas::ColorRole;
namespace style = vesper::desktop::style;
using visor::draw::measure_text;

// The desk's logical size. The framebuffer must be at least this big; a
// bigger one shows the desk in its top-left corner, with 1:1 pixels. The
// VM's DRM driver can choose a larger mode than the VESA boot request.
constexpr SizeI kScreen{.w = 640, .h = 480};
constexpr RectI kScreenBounds{.pos = PointI{0, 0}, .size = kScreen};
constexpr RectI kWorkArea{
    .pos = PointI{0, 0},
    .size = SizeI{.w = kScreen.w, .h = kScreen.h - style::kPanelHeight},
};
using Frame = visor::canvas::Canvas<kScreen>;

// Ink on a lit panel: the desk's light theme is black marks on white, the
// pair visor's cinux constants already spell.
constexpr std::uint32_t kInkOn = visor::host::cinux::kXrgbBlack;
constexpr std::uint32_t kInkOff = visor::host::cinux::kXrgbWhite;

const visor::ui::Theme kDeskTheme = [] {
    visor::ui::Theme theme = visor::ui::kLightTheme;
    theme.corner_radius = visor::ui::CornerRadius{
        .top_left = 2,
        .top_right = 2,
        .bottom_right = 2,
        .bottom_left = 2,
    };
    return theme;
}();

/// The static half of the welcome window, the same panel the X11 lane
/// shows; the two examples keep their own copies so each stays a whole,
/// readable assembly of its own host.
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
            "A desktop on a bare framebuffer.",
            "Double-click an icon to open.",
            "Drag a title bar to move a window.",
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

/// The open reaction, the focus ring plumbing the desk shares with the X11
/// lane: showing, lifting, wearing the band, taking the keyboard.
struct Opener {
    WindowFrame* win;
    WindowFrame* other = nullptr;
    visor::ui::Widget* take_focus = nullptr;
};

void focus_frame(WindowFrame& self, const Opener& opener) noexcept {
    self.set_active(true);
    if (opener.other != nullptr) {
        opener.other->set_active(false);
    }
}

// NOLINTNEXTLINE(misc-const-correctness): the handler type is visor's void*.
void on_frame_focus(WindowFrame& self, void* context) noexcept {
    const auto* const opener = static_cast<const Opener*>(context);
    focus_frame(self, *opener);
}

// NOLINTNEXTLINE(misc-const-correctness): the handler type is visor's void*.
void open_window(Icon& self, void* context) noexcept {
    static_cast<void>(self);
    const auto* const opener = static_cast<const Opener*>(context);
    opener->win->set_visible(true);
    static_cast<void>(opener->win->raise());
    focus_frame(*opener->win, *opener);
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

/// Everything the host table's callbacks reach in: the two input devices,
/// the framebuffer they feed and paint, and the serial trace switch (the
/// bare lane's only debugger: VESPER_FBDEV_TRACE=1 prints every event and
/// every poll wake to stderr, which /init wires to ttyS0).
struct FbdevSession {
    vesper::host::EvdevInput keyboard;
    vesper::host::EvdevInput pointer;
    vesper::host::FbdevDevice fb;
    bool trace = false;
    unsigned long loops = 0;
};

void trace_event(const FbdevSession& session, const visor::host::Event& event) noexcept {
    if (!session.trace) {
        return;
    }
    struct Line {
        const char* name;
        std::uint32_t a;
        int b;
        int c;
    } line{.name = "other", .a = 0, .b = 0, .c = 0};
    if (const auto* down = std::get_if<visor::host::KeyDown>(&event)) {
        line = {
            .name = "keydown",
            .a = down->code,
            .b = down->modifiers.shift ? 1 : 0,
            .c = down->modifiers.ctrl ? 1 : 0,
        };
    } else if (const auto* up = std::get_if<visor::host::KeyUp>(&event)) {
        line = {.name = "keyup", .a = up->code, .b = 0, .c = 0};
    } else if (const auto* move = std::get_if<visor::host::PointerMove>(&event)) {
        line = {.name = "move", .a = 0, .b = move->pos.x, .c = move->pos.y};
    } else if (const auto* down = std::get_if<visor::host::PointerDown>(&event)) {
        line = {.name = "pdown", .a = down->button, .b = down->pos.x, .c = down->pos.y};
    } else if (const auto* up = std::get_if<visor::host::PointerUp>(&event)) {
        line = {.name = "pup", .a = up->button, .b = up->pos.x, .c = up->pos.y};
    } else if (const auto* wheel = std::get_if<visor::host::PointerWheel>(&event)) {
        line = {.name = "wheel", .a = 0, .b = wheel->dx, .c = wheel->dy};
    }
    std::println("trace[{}] {} {} {} {}", session.loops, line.name, line.a, line.b, line.c);
}

void fbdev_flush(void* context, const visor::host::FlushRegion& region) noexcept {
    auto* const session = static_cast<FbdevSession*>(context);
    session->fb.expand(region.rect, region.pixels, region.stride_bytes, kInkOn, kInkOff);
}

bool fbdev_poll_event(void* context, visor::host::Event& out) noexcept {
    auto* const session = static_cast<FbdevSession*>(context);
    // The keyboard goes first: typing takes the round when both devices
    // have something to say, and the pointer's words wait in their node.
    const bool found = session->keyboard.poll_next(out) || session->pointer.poll_next(out);
    if (found) {
        trace_event(*session, out);
    }
    return found;
}

std::uint64_t fbdev_clock_ms(void* /*context*/) noexcept {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

} // namespace

int main(int argc, const char* const* argv) {
    const char* fb_path = argc > 1 ? argv[1] : "/dev/fb0";
    const char* kbd_path = argc > 2 ? argv[2] : "/dev/input/event0";
    const char* pointer_path = argc > 3 ? argv[3] : "/dev/input/event1";

    FbdevSession session;
    if (!session.fb.open(fb_path)) {
        std::println(stderr, "vesper-fbdev: cannot open a 32bpp framebuffer at {}", fb_path);
        return 1;
    }
    if (session.fb.width() < kScreen.w || session.fb.height() < kScreen.h) {
        std::println(stderr, "vesper-fbdev: framebuffer is {}x{}, the desk needs at least {}x{}",
                     session.fb.width(), session.fb.height(), kScreen.w, kScreen.h);
        return 1;
    }
    if (!session.keyboard.open(kbd_path, kScreen)) {
        std::println(stderr, "vesper-fbdev: cannot open the keyboard at {}", kbd_path);
        return 1;
    }
    // The desktop is painted 1:1 in the framebuffer's top-left corner;
    // tablet coordinates span the entire physical display, not that canvas.
    const SizeI display{.w = session.fb.width(), .h = session.fb.height()};
    if (!session.pointer.open(pointer_path, display)) {
        std::println(stderr, "vesper-fbdev: cannot open the pointer at {}", pointer_path);
        return 1;
    }
    std::println("vesper-fbdev: fb {}x{} (frame {}x{}), kbd {}, pointer {}", session.fb.width(),
                 session.fb.height(), kScreen.w, kScreen.h, kbd_path, pointer_path);

    Desktop desktop{kScreenBounds, "VESPER", vesper::fonts::title::kTitleFont, kDeskTheme};
    const SizeI tag_size = measure_text(vesper::fonts::ui::kUiFont, "MONO 1", 1);
    const PointI tag_origin{
        kScreen.w - style::kContentPadding - tag_size.w,
        kScreen.h - style::kPanelHeight +
            ((style::kPanelHeight - static_cast<int>(vesper::fonts::ui::kUiFont.line_height)) / 2),
    };
    visor::ui::Label tag{tag_origin, "MONO 1", vesper::fonts::ui::kUiFont, ColorRole::Background};

    const RectI welcome_slot{
        .pos = PointI{16, 16},
        .size = SizeI{.w = style::kIconSlotWidth, .h = style::kIconSlotHeight},
    };
    const RectI term_slot{
        .pos = PointI{16, 84},
        .size = SizeI{.w = style::kIconSlotWidth, .h = style::kIconSlotHeight},
    };
    Icon welcome_icon{welcome_slot, vesper::examples::kWindowArt, "WELCOME",
                      vesper::fonts::ui::kUiFont};
    Icon term_icon{term_slot, vesper::examples::kTermArt, "TERMINAL", vesper::fonts::ui::kUiFont};

    WelcomePanel welcome{RectI{.pos = PointI{180, 70}, .size = SizeI{.w = 230, .h = 100}},
                         vesper::fonts::title::kTitleFont, vesper::fonts::ui::kUiFont};
    WindowFrame welcome_win{RectI{.pos = PointI{160, 50}, .size = SizeI{.w = 270, .h = 130}},
                            "About", vesper::fonts::title::kTitleFont, kWorkArea};
    Opener welcome_opener{.win = &welcome_win};

    WindowFrame term_win{RectI{.pos = PointI{20, 40}, .size = SizeI{.w = 600, .h = 400}},
                         "Terminal", vesper::fonts::title::kTitleFont, kWorkArea};
    Terminal term{term_win.content_area(), vesper::fonts::term_font::kTermFont};
    // The program behind the screen: a plain POSIX shell on a pty. busybox
    // ash is the initramfs citizen; /bin/sh is the same citizen on a dev
    // machine's console run. TERM=dumb keeps the prompt calm, PS1 is a
    // plain "> " the font can draw.
    constexpr std::array<const char*, 2> sh_args{"sh", "-i"};
    constexpr std::array<const char*, 2> sh_env{"TERM=dumb", "PS1=> "};
    auto sh_session = vesper::host::PtySession::open("/bin/sh", sh_args, sh_env);
    if (!sh_session.has_value()) {
        std::println(stderr, "vesper-fbdev: cannot spawn /bin/sh under a pty: {}",
                     std::strerror(errno));
        return 1;
    }
    PtySource term_source{term, *sh_session};
    term.set_source(&term_source);
    Opener term_opener{.win = &term_win, .take_focus = &term};

    welcome_opener.other = &term_win;
    term_opener.other = &welcome_win;
    term_win.set_on_focus(&on_frame_focus, &term_opener);
    welcome_win.set_on_focus(&on_frame_focus, &welcome_opener);

    Selection selection;
    welcome_icon.set_on_activate(&open_window, &welcome_opener);
    term_icon.set_on_activate(&open_window, &term_opener);
    welcome_icon.set_on_select(&select_icon, &selection);
    term_icon.set_on_select(&select_icon, &selection);

    if (!desktop.add_child(&tag) || !desktop.add_child(&welcome_icon) ||
        !desktop.add_child(&term_icon) || !welcome_win.initialize() ||
        !welcome_win.set_content(&welcome) || !desktop.add_child(&welcome_win) ||
        !term_win.initialize() || !term_win.set_content(&term) || !desktop.add_child(&term_win)) {
        std::println(stderr, "vesper-fbdev: the desktop's child table is full");
        return 1;
    }
    welcome_win.close();
    term_win.close();

    visor::ui::InputRouter input;
    visor::ui::DirtyRegions dirty;
    desktop.set_dirty_sink(&dirty);
    dirty.push(kScreenBounds);

    const visor::host::HostTable table{
        .flush = &fbdev_flush,
        .poll_event = &fbdev_poll_event,
        .clock_ms = &fbdev_clock_ms,
        .context = &session,
    };

    std::println("vesper-fbdev: running; /init's timeout or Ctrl-C ends the run");
    static_cast<void>(std::fflush(stdout));
    Frame frame;
    const char* trace = std::getenv("VESPER_FBDEV_TRACE");
    session.trace = trace != nullptr && std::strcmp(trace, "1") == 0;
    for (;;) {
        // One poll is the loop's sleep: input words, the shell's output,
        // whichever of the three has cargo wakes the desk, a quiet desk
        // sleeps the tick out. A shell that has exited leaves the master
        // hung up forever, so its slot leaves the poll (a negative fd is
        // ignored) -- otherwise every tick would wake on the same hangup.
        ++session.loops;
        const bool shell_alive = !sh_session->child_gone();
        std::array<struct pollfd, 3> waiters{
            {
                {.fd = session.keyboard.fd(), .events = POLLIN, .revents = 0},
                {.fd = session.pointer.fd(), .events = POLLIN, .revents = 0},
                {.fd = shell_alive ? sh_session->master_fd() : -1, .events = POLLIN, .revents = 0},
            },
        };
        const int woke = ::poll(waiters.data(), static_cast<nfds_t>(waiters.size()), 15);
        const int consumed = visor::ui::run_dirty(table, desktop, input, frame, kScreen, dirty);
        if (session.trace && (woke != 0 || consumed != 0)) {
            std::println("trace[{}] poll={} used={} kbd={} ptr={} pty={}", session.loops, woke,
                         consumed, waiters[0].revents, waiters[1].revents, waiters[2].revents);
        } else if (session.trace && session.loops % 320 == 0) {
            // ~5s heartbeat: proves the loop itself is alive between events.
            std::println("trace[{}] beat", session.loops);
        }
        const auto wake_mask = static_cast<unsigned>(POLLIN | POLLHUP | POLLERR);
        if (shell_alive && (static_cast<unsigned>(waiters[2].revents) & wake_mask) != 0) {
            static_cast<void>(term_source.pump(0));
        }
    }
}
