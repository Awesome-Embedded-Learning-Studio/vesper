#include "cases.hpp"
#include "testing.hpp"

#include "desktop/desktop.hpp"
#include "fonts/demo_font.hpp"
#include "host/fake_session_host.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/mono_canvas.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/run_loop.hpp"
#include "visor/ui/theme.hpp"

#include <cstddef>

namespace {

using vesper::desktop::Desktop;
using vesper::host::FakeSessionHost;
using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;

constexpr SizeI kScreen{.w = 480, .h = 320};
constexpr RectI kScreenBounds{.pos = PointI{0, 0}, .size = kScreen};
using Frame = visor::canvas::Canvas<kScreen>;
using Strip = visor::canvas::Canvas<SizeI{.w = 480, .h = 64}>;

} // namespace

void run_desktop_tests() {
    // D0 flagship judgment: one full frame off screen. The light theme keeps
    // the ground unlit, so the banner is the only ink a correct frame may
    // carry -- every lit pixel on the host must lie inside the banner's box,
    // and the banner must have drawn at least one.
    FakeSessionHost host{480, 320};
    Desktop desktop{kScreenBounds, "VESPER", vesper::fonts::kDemoFont, visor::ui::kLightTheme};
    Frame frame;
    visor::ui::render_window(desktop, frame, kScreenBounds, host.table());
    VESPER_CHECK_EQ(host.flush_count(), 1);
    VESPER_CHECK(host.regions()[0] == kScreenBounds);

    const RectI banner_bounds = desktop.banner().bounds();
    int lit_inside = 0;
    int lit_outside = 0;
    for (int y = 0; y < kScreen.h; ++y) {
        for (int x = 0; x < kScreen.w; ++x) {
            if (!host.get(PointI{x, y})) {
                continue;
            }
            if (banner_bounds.contains(PointI{x, y})) {
                ++lit_inside;
            } else {
                ++lit_outside;
            }
        }
    }
    VESPER_CHECK(lit_inside > 0);
    VESPER_CHECK_EQ(lit_outside, 0);

    // The dirty path: the first frame is the caller's debt, one round pays
    // it, and a round that changed nothing submits nothing.
    FakeSessionHost dirty_host{480, 320};
    Desktop dirty_desktop{kScreenBounds, "VESPER", vesper::fonts::kDemoFont,
                          visor::ui::kLightTheme};
    visor::ui::InputRouter input;
    visor::ui::DirtyRegions dirty;
    dirty_desktop.set_dirty_sink(&dirty);
    dirty.push(kScreenBounds);
    Strip strip;
    const int consumed =
        visor::ui::run_dirty(dirty_host.table(), dirty_desktop, input, strip, kScreen, dirty);
    VESPER_CHECK_EQ(consumed, 0);
    VESPER_CHECK(dirty_host.flush_count() > 0);
    VESPER_CHECK_EQ(dirty.count(), std::size_t{0});

    const int before = dirty_host.flush_count();
    const int idle =
        visor::ui::run_dirty(dirty_host.table(), dirty_desktop, input, strip, kScreen, dirty);
    VESPER_CHECK_EQ(idle, 0);
    VESPER_CHECK_EQ(dirty_host.flush_count(), before);
    VESPER_CHECK_EQ(dirty.count(), std::size_t{0});
}
