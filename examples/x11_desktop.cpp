// The eyeball lane: the desk with one icon and one window frame, live on X11.
// The acceptance builds it, the human runs it (a display is needed); a double
// click on the icon opens the window, its X box hides it again, quitting is
// the window manager's X button.
#include "desktop/desktop.hpp"
#include "desktop/icon.hpp"
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

#include <array>
#include <chrono>
#include <cstdint>
#include <print>
#include <thread>
#include <utility>

namespace {

using vesper::desktop::Desktop;
using vesper::desktop::Icon;
using vesper::desktop::WindowFrame;
using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;

constexpr SizeI kScreen{.w = 480, .h = 320};
constexpr RectI kScreenBounds{.pos = PointI{0, 0}, .size = kScreen};
using Frame = visor::canvas::Canvas<kScreen>;

// The icon's art: an 8x8 hollow square with two dots, canvas packing.
constexpr std::array<std::uint8_t, 8> kArtBits{
    0xFF, 0x81, 0xA5, 0x81, 0x81, 0xA5, 0x81, 0xFF,
};
constexpr visor::draw::MonoBitmap kArt{
    .width = 8,
    .height = 8,
    .rows = kArtBits,
};

/// The open reaction: showing the window. The frame is adopted but hidden at
/// startup, so opening is a visibility flip, and the flip reports its own
/// repaint -- no push owed here.
struct Opener {
    WindowFrame* win;
};

void open_window(Icon& self, void* context) noexcept {
    static_cast<void>(self);
    static_cast<Opener*>(context)->win->set_visible(true);
}

} // namespace

int main() {
    auto opened = visor::host::x11::X11WindowHost::open(nullptr, kScreen.w, kScreen.h, 2);
    if (!opened.has_value()) {
        std::println(stderr, "vesper: cannot open the display (is DISPLAY set?)");
        return 1;
    }
    auto host = std::move(*opened);

    Desktop desktop{kScreenBounds, "VESPER DESKTOP", vesper::fonts::kDemoFont,
                    visor::ui::kLightTheme};
    Icon icon{RectI{.pos = PointI{20, 20}, .size = SizeI{.w = 48, .h = 32}}, kArt, "DEMO",
              vesper::fonts::kDemoFont};
    visor::ui::Label words{PointI{0, 0}, "HELLO FROM VESPER", vesper::fonts::kDemoFont};
    WindowFrame win{RectI{.pos = PointI{160, 90}, .size = SizeI{.w = 160, .h = 120}}, "DEMO",
                    vesper::fonts::kDemoFont, kScreenBounds};
    Opener opener{.win = &win};
    icon.set_on_activate(&open_window, &opener);
    if (!win.initialize() || !win.set_content(&words) || !desktop.add_child(&win) ||
        !desktop.add_child(&icon)) {
        std::println(stderr, "vesper: the desktop's child table is full");
        return 1;
    }
    win.close(); // the desk starts with the window hidden; the icon opens it

    visor::ui::InputRouter input;
    visor::ui::DirtyRegions dirty;
    desktop.set_dirty_sink(&dirty);
    dirty.push(kScreenBounds);
    Frame frame;
    std::println("vesper: double-click the icon to open the window, its X box hides it; "
                 "quit with the window X");
    while (!host.close_requested()) {
        static_cast<void>(
            visor::ui::run_dirty(host.table(), desktop, input, frame, kScreen, dirty));
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    return 0;
}
