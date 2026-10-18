// The eyeball lane: the D0 desktop with one D1 window frame, live on X11.
// The acceptance builds it, the human runs it (a display is needed); the
// close box hides the window, quitting is the window manager's X button.
#include "desktop/desktop.hpp"
#include "desktop/window_frame.hpp"
#include "fonts/demo_font.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/x11/x11_window_host.hpp"
#include "visor/canvas/mono_canvas.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/run_loop.hpp"
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/label/label.hpp"

#include <chrono>
#include <print>
#include <thread>
#include <utility>

namespace {

using vesper::desktop::Desktop;
using vesper::desktop::WindowFrame;
using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;

constexpr SizeI kScreen{.w = 480, .h = 320};
constexpr RectI kScreenBounds{.pos = PointI{0, 0}, .size = kScreen};
using Frame = visor::canvas::Canvas<kScreen>;

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
    visor::ui::Label words{PointI{0, 0}, "HELLO FROM VESPER", vesper::fonts::kDemoFont};
    WindowFrame win{RectI{.pos = PointI{40, 50}, .size = SizeI{.w = 160, .h = 120}}, "DEMO",
                    vesper::fonts::kDemoFont};
    if (!win.initialize() || !win.set_content(&words) || !desktop.add_child(&win)) {
        std::println(stderr, "vesper: the desktop's child table is full");
        return 1;
    }
    visor::ui::InputRouter input;
    visor::ui::DirtyRegions dirty;
    desktop.set_dirty_sink(&dirty);
    dirty.push(kScreenBounds);
    Frame frame;
    std::println("vesper: drag the title band, close with the X box; quit with the window X");
    while (!host.close_requested()) {
        static_cast<void>(
            visor::ui::run_dirty(host.table(), desktop, input, frame, kScreen, dirty));
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    return 0;
}
