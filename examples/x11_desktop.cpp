// The eyeball lane: the desk with two icons and two window frames, live on
// X11. The acceptance builds it, the human runs it (a display is needed); a
// double click on an icon opens that icon's window and lifts it to the top,
// its X box hides it again, quitting is the window manager's X button.
#include "base/libpbm/pbm.hpp"
#include "fonts/term_font.hpp"
#include "fonts/title_font.hpp"
#include "fonts/ui_font.hpp"
#include "host/pty.hpp"
#include "icons/desktop_icons.hpp"
#include "ui/desktop/desktop.hpp"
#include "ui/icon/icon.hpp"
#include "ui/pty_source/pty_source.hpp"
#include "ui/style/style.hpp"
#include "ui/style/theme.hpp"
#include "ui/terminal/terminal.hpp"
#include "ui/window_frame/window_frame.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/display.hpp"
#include "visor/bindings/x11/x11_window_host.hpp"
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

#include <array>
#include <cstdint>
#include <print>
#include <utility>
#include <vector>

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
    visor::ui::Widget* take_focus = nullptr;
};

/// The desk's focus ring: exactly one frame wears the accent band. The
/// Opener already knows which pair of frames the ring shares.
void focus_frame(WindowFrame& self, const Opener& opener) noexcept {
    self.set_active(true);
    if (opener.other != nullptr) {
        opener.other->set_active(false);
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

} // namespace

int main(int argc, char** argv) {
    auto opened = visor::host::x11::X11WindowHost::open(nullptr, kScreen.w, kScreen.h, 2,
                                                        visor::host::PixelFormat::XRGB8888);
    if (!opened.has_value()) {
        std::println(stderr, "vesper: cannot open the display (is DISPLAY set?)");
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
    Icon welcome_icon{welcome_slot, vesper::examples::kWindowArt, "WELCOME",
                      vesper::fonts::ui::kUiFont, visor::canvas::ColorRole::Accent};
    Icon term_icon{term_slot, vesper::examples::kTermArt, "TERMINAL", vesper::fonts::ui::kUiFont,
                   visor::canvas::ColorRole::Muted};

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

    // Each opener turns the other frame's band off when it shows its own.
    welcome_opener.other = &term_win;
    term_opener.other = &welcome_win;
    term_win.set_on_focus(&on_frame_focus, &term_opener);
    welcome_win.set_on_focus(&on_frame_focus, &welcome_opener);

    Selection selection;
    welcome_icon.set_on_activate(&open_window, &welcome_opener);
    term_icon.set_on_activate(&open_window, &term_opener);
    welcome_icon.set_on_select(&select_icon, &selection);
    term_icon.set_on_select(&select_icon, &selection);

    // Icons first, windows after: later children paint on top, so an open
    // window never hides behind an icon it covers.
    if (!desktop.add_child(&tag) || !desktop.add_child(&welcome_icon) ||
        !desktop.add_child(&term_icon) || !welcome_win.initialize() ||
        !welcome_win.set_content(&welcome) || !desktop.add_child(&welcome_win) ||
        !term_win.initialize() || !term_win.set_content(&term) || !desktop.add_child(&term_win)) {
        std::println(stderr, "vesper: the desktop's child table is full");
        return 1;
    }
    // The desk starts with both windows hidden; the icons open them.
    welcome_win.close();
    term_win.close();

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
            visor::ui::run_dirty(host.table(), desktop, input, frame, kScreen, dirty));
        // The pty master is the loop's other sleeper: output from bash wakes
        // the desk between host ticks, a quiet desk sleeps the tick out.
        static_cast<void>(term_source.pump(15));
    }
    return 0;
}
