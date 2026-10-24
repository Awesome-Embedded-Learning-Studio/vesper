// The eyeball lane: the desk with two icons and two window frames, live on
// Wayland -- the X11 lane's same scene on the second real host, and the
// desk code's own proof that the two hosts are interchangeable. Built
// when the wayland pieces are present, run by hand; a double click on an
// icon opens that icon's window and lifts it to the top, its X box hides
// it again, quitting is the window manager's close.
#include "base/libpbm/pbm.hpp"
#include "fonts/term_font.hpp"
#include "fonts/title_font.hpp"
#include "fonts/ui_font.hpp"
#include "host/keymap.hpp"
#include "host/pty.hpp"
#include "icons/desktop_icons.hpp"
#include "session/child_spawn.hpp"
#include "ui/desktop/desktop.hpp"
#include "ui/icon/icon.hpp"
#include "ui/pty_source/pty_source.hpp"
#include "ui/remote_canvas/remote_canvas.hpp"
#include "ui/style/style.hpp"
#include "ui/style/theme.hpp"
#include "ui/terminal/terminal.hpp"
#include "ui/window_frame/window_frame.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/display.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/host.hpp"
#include "visor/bindings/wayland/wayland_window_host.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/pixel_traits.hpp"
#include "visor/draw/mono_bitmap.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/run_loop.hpp"
#include "visor/ui/text.hpp"
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/label/label.hpp"
#include "visor/ui/widgets/widget.hpp"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <print>
#include <utility>
#include <vector>

namespace {

using vesper::desktop::Desktop;
using vesper::desktop::Icon;
using vesper::desktop::PtySource;
using vesper::desktop::RemoteCanvas;
using vesper::desktop::Terminal;
using vesper::desktop::WindowFrame;
using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
using visor::canvas::ColorRole;
namespace style = vesper::desktop::style;
using visor::draw::measure_text;

constexpr SizeI kScreen{.w = 480, .h = 320};
constexpr RectI kScreenBounds{.pos = PointI{0, 0}, .size = kScreen};
// The panel keeps the bottom strip: frames drag inside the work area, so a
// window can never park on top of the desk's own chrome.
constexpr RectI kWorkArea{
    .pos = PointI{0, 0},
    .size = SizeI{.w = kScreen.w, .h = kScreen.h - style::kPanelHeight},
};
using Frame = visor::canvas::Canvas<kScreen, visor::canvas::Xrgb8888Traits>;

// The color face: dusk gold over warm paper, the scheme file's own table.
constexpr const visor::ui::Theme& desk_theme = style::kColorDeskTheme;

/// One column of three lines: a heading, a rule and two body lines in the
/// smaller face. A plain Widget because the frame adopts exactly one content
/// widget and this content is static; three Label children would have to be
/// re-laid on every drag.
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
            "A desktop on a one-bit canvas.",
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

/// The open reaction: showing the window, lifting it over its siblings,
/// handing it the active band and the keyboard. The visibility flip, the
/// raise and the band flip report their own repaint -- no push owed here.
struct Opener {
    WindowFrame* win;
    WindowFrame* other = nullptr;
    /// The desk's current block window, when this opener must quiet it
    /// too: the block frame is born on the heap, so the opener holds the
    /// slot (a pointer into the block session) rather than the frame, and
    /// a retired-but-resident block still reads as quietable.
    WindowFrame* const* block_slot = nullptr;
    visor::ui::Widget* take_focus = nullptr;
};

/// The desk's focus ring: exactly one frame wears the accent band. The
/// Opener names its peers -- the pair built at startup plus whichever
/// block window the session currently shows.
void focus_frame(WindowFrame& self, const Opener& opener) noexcept {
    self.set_active(true);
    if (opener.other != nullptr) {
        opener.other->set_active(false);
    }
    if (opener.block_slot != nullptr && *opener.block_slot != nullptr &&
        *opener.block_slot != &self) {
        (*opener.block_slot)->set_active(false);
    }
}

/// The frame's press cue, adapted to the plain function-pointer vocabulary.
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

/// The wayland host speaks raw evdev key numbers (visor's own line: the
/// binding that knows a layout produces text, and the wayland binding
/// deliberately does not), while this desk's terminal eats code points --
/// the X11 lane never meets the gap because XLookupString folds its keymaps
/// inside the host. The fbdev lane answered the same gap with the US text
/// table, and this desk reuses it: a wrapped table whose poll passes every
/// word through the fold before the desk sees it. Flush and clock pass
/// straight through their own thunks, each carrying its own context.
class FoldedKeys {
  public:
    explicit FoldedKeys(visor::host::HostTable wrapped) noexcept : wrapped_{wrapped} {}

    [[nodiscard]] visor::host::HostTable table() noexcept {
        return visor::host::HostTable{
            .flush = &flush_thunk,
            .poll_event = &poll_thunk,
            .clock_ms = &clock_thunk,
            .context = this,
            .info = wrapped_.info,
        };
    }

  private:
    static void flush_thunk(void* context, const visor::host::FlushRegion& region) noexcept {
        const FoldedKeys& self = *static_cast<FoldedKeys*>(context);
        self.wrapped_.flush(self.wrapped_.context, region);
    }

    static bool poll_thunk(void* context, visor::host::Event& out) noexcept {
        const FoldedKeys& self = *static_cast<FoldedKeys*>(context);
        if (!self.wrapped_.poll(out)) {
            return false;
        }
        out = vesper::host::fold_text(out);
        return true;
    }

    static std::uint64_t clock_thunk(void* context) noexcept {
        const FoldedKeys& self = *static_cast<FoldedKeys*>(context);
        return self.wrapped_.now_ms();
    }

    visor::host::HostTable wrapped_;
};

/// The D6a lane: one spawned child program in one desk window. The frame
/// size is the construction-time agreement with d6_block_child (200x120),
/// spelled in the same words on both sides; the child binary is searched
/// beside this program's own image, so the example lane finds it in the
/// build tree without a path baked in. One window at a time: opening the
/// block again while it runs is a no-op, the desk's first honest answer to
/// "what does a second copy mean". Retired frames stay on the tree hidden,
/// the desk's own convention for closed windows; D6b's window list will
/// own them properly.
struct BlockSession {
    WindowFrame* win = nullptr;
    RemoteCanvas* canvas = nullptr;
    pid_t pid = -1;
    Desktop* desk = nullptr;
    std::array<WindowFrame*, 2> peers{nullptr, nullptr}; // the other bands
};

/// Hides the frame and reaps the child: the desk's whole reaction to the
/// channel's end. Runs from inside pump(), which means no repaint debt is
/// owed here -- close() reports its own. The canvas closed its socket the
/// moment the channel died, so this only tends the process and the frame.
void reap_block(BlockSession& session) noexcept;

void retire_block(BlockSession& session) noexcept {
    if (session.win != nullptr) {
        session.win->close();
    }
    session.canvas = nullptr;
    reap_block(session);
}

/// Collects the child without ever waiting: the channel's end already
/// handed it an EOF, so it leaves on its own and the loop sweeps the
/// remainders between frames. Blocking here is the deadlock this lane
/// once shipped -- a full pipe and a waiting desk staring at each other.
void reap_block(BlockSession& session) noexcept {
    // NOLINTNEXTLINE(misc-include-cleaner): WNOHANG is sys/wait.h's own macro.
    if (session.pid > 0 && ::waitpid(session.pid, nullptr, WNOHANG) == session.pid) {
        session.pid = -1;
    }
}

// NOLINTNEXTLINE(misc-const-correctness): the handler type carries void*.
void on_block_exit(RemoteCanvas& self, void* context) noexcept {
    static_cast<void>(self);
    retire_block(*static_cast<BlockSession*>(context));
}

// The close box's answer: hand the child its goodbye word and let the
// channel's own end do the closing -- the EOF wakes the child, the child
// leaves, on_exit retires the frame. Nothing blocks and nothing orphans.
// NOLINTNEXTLINE(misc-const-correctness): the handler type carries void*.
void on_block_close(WindowFrame& self, void* context) noexcept {
    static_cast<void>(self);
    const BlockSession& session = *static_cast<BlockSession*>(context);
    if (session.canvas != nullptr) {
        session.canvas->send_close(); // const calls the const verb, mutates the child
    }
}

// NOLINTNEXTLINE(misc-const-correctness): the handler type carries void*.
void open_block(Icon& self, void* context) noexcept {
    static_cast<void>(self);
    BlockSession& session = *static_cast<BlockSession*>(context);
    if (session.canvas != nullptr) {
        return; // one window at a time
    }
    // The child rides beside this program's own image: whatever path the
    // lane was started from, the sibling binary is the same directory.
    static std::array<char, 512> exe_dir{};
    if (exe_dir.front() == 0) {
        const auto link_len = static_cast<std::size_t>(
            ::readlink("/proc/self/exe", exe_dir.data(), exe_dir.size() - 1));
        if (link_len == 0) {
            return;
        }
        exe_dir[link_len] = 0;
        char* const slash = std::strrchr(exe_dir.data(), '/');
        if (slash != nullptr) {
            slash[1] = 0; // keep the directory, drop the program's own name
        }
    }
    std::array<char, 640> path{};
    const std::size_t dir_len = std::strlen(exe_dir.data());
    std::memcpy(path.data(), exe_dir.data(), dir_len);
    std::memcpy(path.data() + dir_len, "d6_block_child", sizeof("d6_block_child"));
    std::array<char*, 2> argv{path.data(), nullptr};
    auto child = vesper::session::spawn_child(path.data(), argv.data());
    if (!child.has_value()) {
        std::println(stderr, "vesper: cannot spawn the block child at {}", path.data());
        return;
    }
    session.pid = child->pid;
    // The frame and its face are born on the heap and live as long as the
    // tree does -- the same lifetime a stack frame would give them, minus
    // the need to exist before the socket does.
    auto* const win = new WindowFrame{
        RectI{.pos = PointI{150, 90}, .size = SizeI{.w = 210, .h = 145}},
        "Block",
        vesper::fonts::title::kTitleFont,
        kWorkArea,
    };
    auto* const canvas = new RemoteCanvas{win->content_area(), child->socket};
    canvas->set_on_exit(&on_block_exit, &session);
    static_cast<void>(win->initialize());
    static_cast<void>(win->set_content(canvas));
    static_cast<void>(session.desk->add_child(win));
    win->close();           // a newborn window starts hidden like the rest
    win->set_visible(true); // and showing it owes the pixels it claims
    static_cast<void>(win->raise());
    // The band ring: the block takes the accent and the focus, and the
    // frames it shares the desk with give both up -- the same exchange
    // every opener on this desk performs, this one just names two peers.
    win->set_active(true);
    for (WindowFrame* peer : session.peers) {
        if (peer != nullptr) {
            peer->set_active(false);
        }
    }
    canvas->set_focused(true);
    win->set_on_close(&on_block_close, &session);
    session.win = win;
    session.canvas = canvas;
}

} // namespace

int main(int argc, char** argv) {
    auto opened = visor::host::wayland::WaylandWindowHost::open(nullptr, kScreen.w, kScreen.h, 2,
                                                                visor::host::PixelFormat::XRGB8888);
    if (!opened.has_value()) {
        std::println(stderr, "vesper: cannot open the display (is WAYLAND_DISPLAY set?)");
        return 1;
    }
    auto host = std::move(*opened);

    // Optional argv[1]: a P4 bitmap painted as the desk's picture. Declared
    // before the desktop because the wallpaper view borrows this storage.
    vesper::assets::PbmImage wallpaper_image;
    visor::draw::MonoBitmap wallpaper_view{};
    if (argc > 1) {
        std::vector<std::uint8_t> bytes;
        if (!vesper::assets::read_file_bytes(argv[1], bytes)) {
            std::println(stderr, "vesper: cannot read {}", argv[1]);
            return 1;
        }
        wallpaper_image = vesper::assets::decode_pbm(bytes);
        if (!wallpaper_image.ok) {
            std::println(stderr, "vesper: {} is not a valid P4 bitmap", argv[1]);
            return 1;
        }
        wallpaper_view = wallpaper_image.bitmap();
    }

    Desktop desktop{kScreenBounds, "VESPER", vesper::fonts::title::kTitleFont, desk_theme};
    if (wallpaper_image.ok) {
        desktop.set_wallpaper(&wallpaper_view);
    }
    // The panel's right side: the scheme's name, the desk's identity tag.
    const SizeI tag_size = measure_text(vesper::fonts::ui::kUiFont, "MONO 1", 1);
    const PointI tag_origin{
        kScreen.w - style::kContentPadding - tag_size.w,
        kScreen.h - style::kPanelHeight +
            ((style::kPanelHeight - static_cast<int>(vesper::fonts::ui::kUiFont.line_height)) / 2),
    };
    visor::ui::Label tag{tag_origin, "MONO 1", vesper::fonts::ui::kUiFont, ColorRole::Background};

    // The icon column on the desk's left, one slot under the other.
    const RectI welcome_slot{
        .pos = PointI{16, 16},
        .size =
            SizeI{
                .w = style::kIconSlotWidth,
                .h = style::kIconSlotHeight,
            },
    };
    const RectI term_slot{
        .pos = PointI{16, 84},
        .size =
            SizeI{
                .w = style::kIconSlotWidth,
                .h = style::kIconSlotHeight,
            },
    };
    // The layered art: a gold frame with ink glass, a muted body with a
    // gold prompt. Both layers ride the same centered block.
    constexpr std::array<Icon::Layer, 2> welcome_layers = [] {
        std::array<Icon::Layer, 2> layers{};
        layers[0] = {
            .art = &vesper::examples::kWindowFrameArt,
            .ink = visor::canvas::ColorRole::Accent,
        };
        layers[1] = {
            .art = &vesper::examples::kWindowContentArt,
            .ink = visor::canvas::ColorRole::Foreground,
        };
        return layers;
    }();
    constexpr std::array<Icon::Layer, 2> term_layers = [] {
        std::array<Icon::Layer, 2> layers{};
        layers[0] = {
            .art = &vesper::examples::kTermBodyArt,
            .ink = visor::canvas::ColorRole::Muted,
        };
        layers[1] = {
            .art = &vesper::examples::kTermGoldArt,
            .ink = visor::canvas::ColorRole::Accent,
        };
        return layers;
    }();
    Icon welcome_icon{welcome_slot, welcome_layers, "WELCOME", vesper::fonts::ui::kUiFont};
    Icon term_icon{term_slot, term_layers, "TERMINAL", vesper::fonts::ui::kUiFont};
    const RectI block_slot{
        .pos = PointI{16, 152},
        .size = SizeI{.w = style::kIconSlotWidth, .h = style::kIconSlotHeight},
    };
    constexpr std::array<Icon::Layer, 2> block_layers = [] {
        std::array<Icon::Layer, 2> layers{};
        layers[0] = {
            .art = &vesper::examples::kTermBodyArt,
            .ink = visor::canvas::ColorRole::Muted,
        };
        layers[1] = {
            .art = &vesper::examples::kWindowContentArt,
            .ink = visor::canvas::ColorRole::Accent,
        };
        return layers;
    }();
    Icon block_icon{block_slot, block_layers, "BLOCK", vesper::fonts::ui::kUiFont};

    WelcomePanel welcome{RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 210, .h = 87}},
                         vesper::fonts::title::kTitleFont, vesper::fonts::ui::kUiFont};
    WindowFrame welcome_win{RectI{.pos = PointI{120, 36}, .size = SizeI{.w = 220, .h = 112}},
                            "About", vesper::fonts::title::kTitleFont, kWorkArea};
    Opener welcome_opener{.win = &welcome_win};

    // The terminal in its own window, built at content_area() exactly,
    // because the frame re-bounds its content to that same rectangle and the
    // grid is cut at construction.
    WindowFrame term_win{RectI{.pos = PointI{10, 24}, .size = SizeI{.w = 460, .h = 270}},
                         "Terminal", vesper::fonts::title::kTitleFont, kWorkArea};
    Terminal term{term_win.content_area(), vesper::fonts::term_font::kTermFont};
    // The program behind the screen: bash on a pty. --norc keeps the
    // machine's rc files out of a demo, TERM=vt100 gives clear its escape
    // words (a dumb terminal has no clear to send) and readline's arrows a
    // CSI the screen speaks, and PS1 is a plain "> " that the font can draw.
    constexpr std::array<const char*, 3> bash_args{"bash", "--norc", "-i"};
    constexpr std::array<const char*, 2> bash_env{"TERM=vt100", "PS1=> "};
    auto bash_session = vesper::host::PtySession::open("/bin/bash", bash_args, bash_env);
    if (!bash_session.has_value()) {
        std::println(stderr, "vesper: cannot spawn bash under a pty");
        return 1;
    }
    // The line editor redraws against the screen's own grid, not a guessed
    // default: the terminal's cell count is the pty's window size.
    static_cast<void>(bash_session->set_winsize(term.cols(), term.rows()));
    PtySource term_source{term, *bash_session};
    term.set_source(&term_source);
    Opener term_opener{.win = &term_win, .take_focus = &term};

    // Each opener turns the other frames' bands off when it shows its own;
    // the block names its peers on open, and its peers name it back.
    welcome_opener.other = &term_win;
    term_opener.other = &welcome_win;
    term_win.set_on_focus(&on_frame_focus, &term_opener);
    welcome_win.set_on_focus(&on_frame_focus, &welcome_opener);

    Selection selection;
    BlockSession block{.desk = &desktop};
    block.peers[0] = &welcome_win;
    block.peers[1] = &term_win;
    welcome_opener.block_slot = &block.win;
    term_opener.block_slot = &block.win;
    block_icon.set_on_activate(&open_block, &block);
    block_icon.set_on_select(&select_icon, &selection);
    welcome_icon.set_on_activate(&open_window, &welcome_opener);
    term_icon.set_on_activate(&open_window, &term_opener);
    welcome_icon.set_on_select(&select_icon, &selection);
    term_icon.set_on_select(&select_icon, &selection);

    // Icons first, windows after: later children paint on top, so an open
    // window never hides behind an icon it covers.
    if (!desktop.add_child(&tag) || !desktop.add_child(&welcome_icon) ||
        !desktop.add_child(&term_icon) || !desktop.add_child(&block_icon) ||
        !welcome_win.initialize() || !welcome_win.set_content(&welcome) ||
        !desktop.add_child(&welcome_win) || !term_win.initialize() ||
        !term_win.set_content(&term) || !desktop.add_child(&term_win)) {
        std::println(stderr, "vesper: the desktop's child table is full");
        return 1;
    }
    // The desk starts with both windows hidden; the icons open them.
    welcome_win.close();
    term_win.close();

    FoldedKeys keys{host.table()};

    visor::ui::InputRouter input;
    visor::ui::DirtyRegions dirty;
    desktop.set_dirty_sink(&dirty);
    dirty.push(kScreenBounds);
    // The keyboard arrives with the window, handed over by its opener; the
    // first pixels arrive on their own, bash's prompt through the pty.
    Frame frame;
    std::println("vesper: double-click an icon to open its window; the terminal runs "
                 "bash, click into it and type; quit with the window X");
    while (!host.close_requested()) {
        static_cast<void>(
            visor::ui::run_dirty(keys.table(), desktop, input, frame, kScreen, dirty));
        // The pty master is the loop's other sleeper: output from bash wakes
        // the desk between host ticks, a quiet desk sleeps the tick out.
        static_cast<void>(term_source.pump(15));
        if (block.canvas != nullptr) {
            block.canvas->pump();
        }
        reap_block(block);
    }
    return 0;
}
