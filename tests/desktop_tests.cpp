#include "cases.hpp"
#include "testing.hpp"

#include "desktop/desktop.hpp"
#include "fonts/demo_font.hpp"
#include "host/fake_session_host.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/mono_canvas.hpp"
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
