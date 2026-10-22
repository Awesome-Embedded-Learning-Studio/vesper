#include "cases.hpp"
#include "testing.hpp"

#include "fonts/demo_font.hpp"
#include "host/fake_session_host.hpp"
#include "ui/desktop/desktop.hpp"
#include "ui/window_frame/window_frame.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/events/pointer_words.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/run_loop.hpp"
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/label/label.hpp"

#include <initializer_list>

namespace {

using vesper::desktop::Desktop;
using vesper::desktop::WindowFrame;
using vesper::host::FakeSessionHost;
using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;

constexpr SizeI kScreen{.w = 480, .h = 320};
constexpr RectI kScreenBounds{.pos = PointI{0, 0}, .size = kScreen};
using Frame = visor::canvas::Canvas<kScreen>;
using Strip = visor::canvas::Canvas<SizeI{.w = 480, .h = 64}>;

/// One assembled desktop with a window, live against its own fake session.
/// build() lays the first frame down (the caller's debt, one full push) and
/// forgets the region log, so every judgment after it reads one round only.
struct Scene {
    FakeSessionHost host{480, 320};
    visor::ui::InputRouter input{};
    visor::ui::DirtyRegions dirty{};
    Frame frame{};
    Strip strip{};
    Desktop desktop{kScreenBounds, "VESPER", vesper::fonts::kDemoFont, visor::ui::kLightTheme};
    visor::ui::Label content{PointI{0, 0}, "HELLO", vesper::fonts::kDemoFont};
    WindowFrame win{RectI{.pos = PointI{40, 50}, .size = SizeI{.w = 160, .h = 120}}, "DEMO",
                    vesper::fonts::kDemoFont, kScreenBounds};

    /// The strips of the fixed 480x320 screen, top to bottom.
    static constexpr int kStripHeight = 64;

    [[nodiscard]] bool build() {
        if (!win.initialize()) {
            return false;
        }
        if (!win.set_content(&content)) {
            return false;
        }
        if (!desktop.add_child(&win)) {
            return false;
        }
        desktop.set_dirty_sink(&dirty);
        dirty.push(kScreenBounds);
        pump();
        host.clear_regions();
        return true;
    }

    void feed(std::initializer_list<visor::host::Event> events) {
        for (const auto& event : events) {
            VESPER_CHECK(host.push_event(event));
        }
    }

    int pump() { return visor::ui::run_dirty(host.table(), desktop, input, strip, kScreen, dirty); }

    /// True when every screen strip the box spans was submitted this round:
    /// the boundary-side reading of "the box was reported". A widget box is
    /// taller than one strip, so coverage is per strip, never per box.
    [[nodiscard]] bool strips_cover(const RectI& box) const {
        const auto& logged = host.regions();
        for (int top = 0; top < kScreen.h; top += kStripHeight) {
            const int rows = (kScreen.h - top < kStripHeight) ? (kScreen.h - top) : kStripHeight;
            const RectI strip_window{
                .pos = PointI{0, top},
                .size = SizeI{.w = kScreen.w, .h = rows},
            };
            const bool intersects = box.pos.x < strip_window.pos.x + strip_window.size.w &&
                                    strip_window.pos.x < box.pos.x + box.size.w &&
                                    box.pos.y < strip_window.pos.y + strip_window.size.h &&
                                    strip_window.pos.y < box.pos.y + box.size.h;
            if (!intersects) {
                continue;
            }
            bool found = false;
            for (const auto& region : logged) {
                if (region == strip_window) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
        return true;
    }
};

} // namespace

void run_window_frame_tests() {
    // Drag judgment: press the band, wander off the window (capture keeps
    // the moves with the frame), come back, release. The grab offset rides
    // along: grabbing at (80, 55) on a frame at (40, 50) puts the frame's
    // origin 40 right and 5 down of the pointer.
    {
        Scene s;
        VESPER_CHECK(s.build());
        const RectI old_box = s.win.bounds();
        const PointI old_content = s.content.bounds().pos;
        s.host.clear_regions();
        s.feed({
            visor::host::PointerDown{.button = 0, .pos = PointI{80, 55}},
            visor::host::PointerMove{.pos = PointI{300, 200}}, // far outside: capture holds
            visor::host::PointerMove{.pos = PointI{100, 60}},
            visor::host::PointerMove{.pos = PointI{120, 70}},
            visor::host::PointerUp{.button = 0, .pos = PointI{120, 70}},
        });
        VESPER_CHECK_EQ(s.pump(), 5);

        const RectI new_box = RectI{.pos = PointI{80, 65}, .size = SizeI{.w = 160, .h = 120}};
        VESPER_CHECK(s.win.bounds() == new_box);
        // The content rode along: the same delta as the frame's origin.
        VESPER_CHECK((s.content.bounds().pos == PointI{old_content.x + 40, old_content.y + 15}));
        // Old and new boxes were both reported: their strips all resubmitted.
        VESPER_CHECK(s.strips_cover(old_box));
        VESPER_CHECK(s.strips_cover(new_box));
        // Pixels: the old frame is gone, the new one is there. The border
        // pixel lit before the drag and reads unlit after; the band pixel
        // (the grab point itself) lit before and lights at its new home.
        VESPER_CHECK(!s.host.get(PointI{40, 50}));
        VESPER_CHECK(!s.host.get(PointI{80, 55}));
        VESPER_CHECK(s.host.get(PointI{80, 65}));
        VESPER_CHECK(s.host.get(PointI{120, 70}));
    }

    // A press off the band never starts a drag: the content area's press
    // belongs to the content, the desktop's ground press belongs to nobody.
    {
        Scene s;
        VESPER_CHECK(s.build());
        s.feed({
            visor::host::PointerDown{.button = 0, .pos = PointI{100, 100}},
            visor::host::PointerMove{.pos = PointI{150, 120}},
            visor::host::PointerUp{.button = 0, .pos = PointI{150, 120}},
            visor::host::PointerDown{.button = 0, .pos = PointI{10, 10}},
            visor::host::PointerMove{.pos = PointI{60, 40}},
            visor::host::PointerUp{.button = 0, .pos = PointI{60, 40}},
        });
        VESPER_CHECK_EQ(s.pump(), 6);
        VESPER_CHECK(
            (s.win.bounds() == RectI{.pos = PointI{40, 50}, .size = SizeI{.w = 160, .h = 120}}));
    }

    // Close judgment: a click on the close box reclaims the window, the
    // reclaim is reported, and the pixels go back to the ground.
    {
        Scene s;
        VESPER_CHECK(s.build());
        const RectI box = s.win.bounds();
        const RectI close_rect = s.win.close_box();
        const PointI center{close_rect.pos.x + (close_rect.size.w / 2),
                            close_rect.pos.y + (close_rect.size.h / 2)};
        s.host.clear_regions();
        s.feed({
            visor::host::PointerDown{.button = 0, .pos = center},
            visor::host::PointerUp{.button = 0, .pos = center},
        });
        VESPER_CHECK_EQ(s.pump(), 2);
        VESPER_CHECK(!s.win.visible());
        VESPER_CHECK(s.strips_cover(box));
        // Border and band both gave their pixels back.
        VESPER_CHECK(!s.host.get(box.pos));
        VESPER_CHECK(!s.host.get(PointI{80, 55}));
        // A hidden window takes no clicks: the same press lands on nothing
        // of the frame and stays closed.
        s.feed({
            visor::host::PointerDown{.button = 0, .pos = center},
            visor::host::PointerUp{.button = 0, .pos = center},
        });
        VESPER_CHECK_EQ(s.pump(), 2);
        VESPER_CHECK(!s.win.visible());
    }

    // The button's own click synthesis: a release that lands outside the
    // close box cancels, and dragging from the close box is not a drag.
    {
        Scene s;
        VESPER_CHECK(s.build());
        const RectI close_rect = s.win.close_box();
        const PointI center{close_rect.pos.x + (close_rect.size.w / 2),
                            close_rect.pos.y + (close_rect.size.h / 2)};
        s.feed({
            visor::host::PointerDown{.button = 0, .pos = center},
            visor::host::PointerMove{.pos = PointI{60, 20}},
            visor::host::PointerUp{.button = 0, .pos = PointI{60, 20}},
        });
        VESPER_CHECK_EQ(s.pump(), 3);
        VESPER_CHECK(s.win.visible());
        VESPER_CHECK(
            (s.win.bounds() == RectI{.pos = PointI{40, 50}, .size = SizeI{.w = 160, .h = 120}}));
    }
    // The leash: dragging hard off any edge pins the frame inside the
    // desktop instead of stranding it out of reach. The same grab offset
    // rides both corners: pressing (80, 55) on a frame at (40, 50) puts
    // the origin 40 right and 5 down of the pointer, so the clamped
    // landing is derived, not hard-coded.
    {
        Scene s;
        VESPER_CHECK(s.build());
        const PointI old_content = s.content.bounds().pos;
        s.host.clear_regions();
        s.feed({
            visor::host::PointerDown{.button = 0, .pos = PointI{80, 55}},
            visor::host::PointerMove{.pos = PointI{-1000, -1000}},
            visor::host::PointerUp{.button = 0, .pos = PointI{-1000, -1000}},
        });
        VESPER_CHECK_EQ(s.pump(), 3);
        VESPER_CHECK((s.win.bounds().pos == PointI{0, 0}));
        VESPER_CHECK((s.content.bounds().pos == PointI{old_content.x - 40, old_content.y - 50}));
        s.host.clear_regions();
        s.feed({
            visor::host::PointerDown{.button = 0, .pos = PointI{10, 5}},
            visor::host::PointerMove{.pos = PointI{2000, 2000}},
            visor::host::PointerUp{.button = 0, .pos = PointI{2000, 2000}},
        });
        VESPER_CHECK_EQ(s.pump(), 3);
        VESPER_CHECK((s.win.bounds().pos == PointI{kScreen.w - 160, kScreen.h - 120}));
    }
}
