#include "cases.hpp"
#include "testing.hpp"

#include "desktop/desktop.hpp"
#include "fonts/demo_font.hpp"
#include "host/fake_session_host.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/draw/mono_bitmap.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/run_loop.hpp"
#include "visor/ui/theme.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

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

// Wallpaper judgment art: one byte per row of eight, all rows 0xF0, so the
// left half of the top eight screen rows lights (MSB first) and the right
// half stays the ground's.
constexpr std::array<std::uint8_t, 8> kWallpaperBits{
    0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
};
constexpr visor::draw::MonoBitmap kWallpaper{
    .width = 8,
    .height = 8,
    .rows = kWallpaperBits,
};

} // namespace

void run_desktop_tests() {
    // The panel is the one deliberate ink on a fresh desk: every lit pixel
    // must lie inside panel_bounds(), and the panel's fill must be lit.
    // The banner label paints in the background ink on that fill, so its
    // box carries holes rather than extra ink -- a hole proves it drew.
    FakeSessionHost host{480, 320};
    Desktop desktop{kScreenBounds, "VESPER", vesper::fonts::kDemoFont, visor::ui::kLightTheme};
    Frame frame;
    visor::ui::render_window(desktop, frame, kScreenBounds, host.table());
    VESPER_CHECK_EQ(host.flush_count(), 1);
    VESPER_CHECK(host.regions()[0] == kScreenBounds);

    const RectI panel_bounds = desktop.panel_bounds();
    const RectI banner_bounds = desktop.banner().bounds();
    int lit_above_panel = 0;
    int unlit_in_banner = 0;
    for (int y = 0; y < kScreen.h; ++y) {
        for (int x = 0; x < kScreen.w; ++x) {
            const PointI at{x, y};
            const bool lit = host.get(at);
            if (lit && !panel_bounds.contains(at)) {
                ++lit_above_panel;
            }
            if (!lit && banner_bounds.contains(at)) {
                ++unlit_in_banner;
            }
        }
    }
    VESPER_CHECK_EQ(lit_above_panel, 0);
    VESPER_CHECK(host.get(PointI{kScreen.w - 2, panel_bounds.pos.y + 1})); // panel fill
    VESPER_CHECK(unlit_in_banner > 0);                                     // the name cut out of it

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

    // Wallpaper judgment: with a picture set, its lit pixels read lit on the
    // host, the ground beside them reads unlit, and setting the picture owed
    // the whole desk on its own -- no manual push, one round pays it.
    FakeSessionHost paper_host{480, 320};
    Desktop paper_desktop{kScreenBounds, "VESPER", vesper::fonts::kDemoFont,
                          visor::ui::kLightTheme};
    visor::ui::DirtyRegions paper_dirty;
    paper_desktop.set_dirty_sink(&paper_dirty);
    paper_desktop.set_wallpaper(&kWallpaper);
    VESPER_CHECK_EQ(paper_dirty.count(), std::size_t{1}); // the setter owed the desk
    Strip paper_strip;
    visor::ui::InputRouter paper_input;
    static_cast<void>(visor::ui::run_dirty(paper_host.table(), paper_desktop, paper_input,
                                           paper_strip, kScreen, paper_dirty));
    VESPER_CHECK(paper_host.get(PointI{0, 0}));  // wallpaper ink, left half
    VESPER_CHECK(paper_host.get(PointI{3, 7}));  // still inside the picture's lit half
    VESPER_CHECK(!paper_host.get(PointI{4, 0})); // the picture's own unlit half
    VESPER_CHECK(!paper_host.get(PointI{0, 8})); // below the picture: ground
}
