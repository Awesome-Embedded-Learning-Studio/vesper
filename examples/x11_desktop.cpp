// The eyeball lane: the desk with two icons and two window frames, live on
// X11. The acceptance builds it, the human runs it (a display is needed); a
// double click on an icon opens that icon's window and lifts it to the top,
// its X box hides it again, quitting is the window manager's X button.
#include "assets/pbm.hpp"
#include "desktop/desktop.hpp"
#include "desktop/icon.hpp"
#include "desktop/terminal.hpp"
#include "desktop/window_frame.hpp"
#include "fonts/demo_font.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/x11/x11_window_host.hpp"
#include "visor/canvas/mono_canvas.hpp"
#include "visor/draw/mono_bitmap.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/run_loop.hpp"
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/label/label.hpp"
#include "visor/ui/widgets/widget.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <print>
#include <thread>
#include <utility>
#include <vector>

namespace {

using vesper::desktop::Desktop;
using vesper::desktop::Icon;
using vesper::desktop::LoopbackSource;
using vesper::desktop::Terminal;
using vesper::desktop::WindowFrame;
using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;

constexpr SizeI kScreen{.w = 480, .h = 320};
constexpr RectI kScreenBounds{.pos = PointI{0, 0}, .size = kScreen};
using Frame = visor::canvas::Canvas<kScreen>;

// The icons' art: 16x16 canvas packing, two bytes per row, the comment
// beside each row the picture itself (x0 left). The demo window carries
// a V inside, the terminal screen a >_ prompt on a stand.
constexpr std::array<std::uint8_t, 32> kWindowArtBits{
    0x00, 0x00, // ................,
    0x7F, 0xFE, // .11111111111111.,
    0x7F, 0xFE, // .11111111111111.,
    0x7F, 0xFE, // .11111111111111.,
    0x40, 0x02, // .1............1.,
    0x48, 0x12, // .1..1......1..1.,
    0x44, 0x22, // .1...1....1...1.,
    0x42, 0x42, // .1....1..1....1.,
    0x41, 0x82, // .1.....11.....1.,
    0x40, 0x02, // .1............1.,
    0x40, 0x02, // .1............1.,
    0x40, 0x02, // .1............1.,
    0x7F, 0xFE, // .11111111111111.,
    0x00, 0x00, // ................,
    0x00, 0x00, // ................,
    0x00, 0x00, // ................,
};
constexpr visor::draw::MonoBitmap kWindowArt{
    .width = 16,
    .height = 16,
    .rows = kWindowArtBits,
};

constexpr std::array<std::uint8_t, 32> kTermArtBits{
    0x00, 0x00, // ................,
    0x7F, 0xFE, // .11111111111111.,
    0x7F, 0xFE, // .11111111111111.,
    0x40, 0x02, // .1............1.,
    0x48, 0x02, // .1..1.........1.,
    0x4C, 0x02, // .1..11........1.,
    0x42, 0x02, // .1....1.......1.,
    0x4C, 0x02, // .1..11........1.,
    0x48, 0x02, // .1..1.........1.,
    0x40, 0x02, // .1............1.,
    0x40, 0xE2, // .1......111...1.,
    0x40, 0x02, // .1............1.,
    0x7F, 0xFE, // .11111111111111.,
    0x01, 0x80, // .......11.......,
    0x07, 0xE0, // .....111111.....,
    0x00, 0x00, // ................,
};
constexpr visor::draw::MonoBitmap kTermArt{
    .width = 16,
    .height = 16,
    .rows = kTermArtBits,
};

/// The open reaction: showing the window, lifting it over its siblings, and
/// handing the keyboard to the content that wants it. The visibility flip
/// and the raise report their own repaint -- no push owed here.
struct Opener {
    WindowFrame* win;
    visor::ui::Widget* take_focus = nullptr;
};

void open_window(Icon& self, void* context) noexcept {
    static_cast<void>(self);
    const Opener& opener = *static_cast<Opener*>(context);
    opener.win->set_visible(true);
    static_cast<void>(opener.win->raise());
    if (opener.take_focus != nullptr) {
        opener.take_focus->set_focused(true);
    }
}

} // namespace

int main(int argc, char** argv) {
    auto opened = visor::host::x11::X11WindowHost::open(nullptr, kScreen.w, kScreen.h, 2);
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

    Desktop desktop{kScreenBounds, "VESPER DESKTOP", vesper::fonts::kDemoFont,
                    visor::ui::kLightTheme};
    if (wallpaper_image.ok) {
        desktop.set_wallpaper(&wallpaper_view);
    }
    // The icon column on the desk's left, one card under the other.
    Icon demo_icon{RectI{.pos = PointI{16, 16}, .size = SizeI{.w = 48, .h = 40}}, kWindowArt,
                   "DEMO", vesper::fonts::kDemoFont};
    Icon term_icon{RectI{.pos = PointI{16, 68}, .size = SizeI{.w = 48, .h = 40}}, kTermArt, "TERM",
                   vesper::fonts::kDemoFont};
    visor::ui::Label words{PointI{0, 0}, "HELLO FROM VESPER", vesper::fonts::kDemoFont};
    WindowFrame win{RectI{.pos = PointI{160, 90}, .size = SizeI{.w = 160, .h = 120}}, "DEMO",
                    vesper::fonts::kDemoFont, kScreenBounds};
    Opener demo_opener{.win = &win};
    demo_icon.set_on_activate(&open_window, &demo_opener);
    // The D3 screen: a terminal in its own window, typing echoed by the
    // loopback fake source. Built at content_area() exactly, because the
    // frame re-bounds its content to that same rectangle and the grid is
    // cut at construction.
    WindowFrame term_win{RectI{.pos = PointI{4, 60}, .size = SizeI{.w = 472, .h = 256}}, "TERM",
                         vesper::fonts::kDemoFont, kScreenBounds};
    Terminal term{term_win.content_area(), vesper::fonts::kDemoFont};
    LoopbackSource term_source{term};
    term.set_source(&term_source);
    Opener term_opener{.win = &term_win, .take_focus = &term};
    term_icon.set_on_activate(&open_window, &term_opener);
    // Icons first, windows after: later children paint on top, so an open
    // window never hides behind an icon it covers.
    if (!desktop.add_child(&demo_icon) || !desktop.add_child(&term_icon) || !win.initialize() ||
        !win.set_content(&words) || !desktop.add_child(&win) || !term_win.initialize() ||
        !term_win.set_content(&term) || !desktop.add_child(&term_win)) {
        std::println(stderr, "vesper: the desktop's child table is full");
        return 1;
    }
    // The desk starts with both windows hidden; the icons open them.
    win.close();
    term_win.close();

    visor::ui::InputRouter input;
    visor::ui::DirtyRegions dirty;
    desktop.set_dirty_sink(&dirty);
    dirty.push(kScreenBounds);
    // The greeting rides the same receive() door D4's program output will;
    // the keyboard arrives with the window, handed over by its opener.
    term.receive("VESPER TERM\nD3 LOOPBACK ECHO\n> ");
    Frame frame;
    std::println("vesper: double-click an icon to open its window, click into the "
                 "terminal and type (it echoes), quit with the window X");
    while (!host.close_requested()) {
        static_cast<void>(
            visor::ui::run_dirty(host.table(), desktop, input, frame, kScreen, dirty));
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    return 0;
}
