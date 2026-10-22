#include "cases.hpp"
#include "testing.hpp"

#include "fonts/demo_font.hpp"
#include "host/evdev_input.hpp"
#include "host/fake_session_host.hpp"
#include "ui/desktop/desktop.hpp"
#include "ui/icon/icon.hpp"
#include "ui/window_frame/window_frame.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/events/pointer_words.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/draw/mono_bitmap.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/run_loop.hpp"
#include "visor/ui/theme.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace {

using vesper::desktop::Desktop;
using vesper::desktop::Icon;
using vesper::desktop::WindowFrame;
using vesper::host::FakeSessionHost;
using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;

constexpr SizeI kScreen{.w = 480, .h = 320};
constexpr RectI kScreenBounds{.pos = PointI{0, 0}, .size = kScreen};
using Strip = visor::canvas::Canvas<SizeI{.w = 480, .h = 64}>;

// The icon's art: an 8x8 hollow square with two dots, one packed byte per
// row in the canvas packing (MSB first). Small on purpose -- the judgments
// read pixels at known spots, and a dense block would make "which pixel"
// ambiguous.
constexpr std::array<std::uint8_t, 8> kArtBits{
    0xFF, 0x81, 0xA5, 0x81, 0x81, 0xA5, 0x81, 0xFF,
};
constexpr visor::draw::MonoBitmap kArt{
    .width = 8,
    .height = 8,
    .rows = kArtBits,
};

constexpr RectI kIconBounds{.pos = PointI{20, 20}, .size = SizeI{.w = 48, .h = 32}};
constexpr PointI kIconCenter{kIconBounds.pos.x + (kIconBounds.size.w / 2),
                             kIconBounds.pos.y + (kIconBounds.size.h / 2)};
constexpr RectI kWinBounds{.pos = PointI{200, 120}, .size = SizeI{.w = 120, .h = 80}};

/// One desk with an icon wired to open a window, live against its own fake
/// session. The window is built but stranger to the tree until the icon's
/// double click adopts it, so "open" is observable as tree membership.
struct Scene {
    FakeSessionHost host{480, 320};
    visor::ui::InputRouter input{};
    visor::ui::DirtyRegions dirty{};
    Strip strip{};
    Desktop desktop{kScreenBounds, "VESPER", vesper::fonts::kDemoFont, visor::ui::kLightTheme};
    Icon icon{kIconBounds, kArt, "DEMO", vesper::fonts::kDemoFont};
    WindowFrame win{kWinBounds, "APP", vesper::fonts::kDemoFont, kScreenBounds};
    int opened = 0;

    /// The open reaction: what a desktop does with the gesture. Adoption
    /// wires the sink and the theme but reports nothing (add_child's
    /// contract), so the opener owes the repaint of what just appeared --
    /// pushing the window's box is this side of the deal, not the tree's.
    static void on_open(Icon& self, void* context) noexcept {
        static_cast<void>(self);
        Scene& scene = *static_cast<Scene*>(context);
        ++scene.opened;
        static_cast<void>(scene.desktop.add_child(&scene.win));
        scene.dirty.push(scene.win.bounds());
    }

    [[nodiscard]] bool build() {
        icon.set_on_activate(&Scene::on_open, this);
        if (!desktop.add_child(&icon)) {
            return false;
        }
        if (!win.initialize()) {
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
            VESPER_CHECK((host.push_event(event)));
        }
    }

    int pump() { return visor::ui::run_dirty(host.table(), desktop, input, strip, kScreen, dirty); }

    /// True when every screen strip the box spans was submitted this round,
    /// the boundary-side reading of "the box was reported". A widget box is
    /// taller than one strip, so coverage is per strip, never per box.
    [[nodiscard]] bool strips_cover(const RectI& box) const {
        const auto& logged = host.regions();
        for (int top = 0; top < kScreen.h; top += 64) {
            const int rows = (kScreen.h - top < 64) ? (kScreen.h - top) : 64;
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

void run_icon_tests() {
    // Open judgment: a double click on the icon adopts the window into the
    // tree, the opener's push reports it, and the pixels arrive. Both clicks
    // ride one pump, so the pair's clock reading is shared and the time leg
    // of the gesture holds by construction; the distance leg holds the same
    // way -- same spot twice.
    {
        Scene s;
        VESPER_CHECK(s.build());
        VESPER_CHECK_EQ(s.desktop.child_count(), std::size_t{2}); // banner and icon
        s.feed({
            visor::host::PointerDown{.button = 1, .pos = kIconCenter},
            visor::host::PointerUp{.button = 1, .pos = kIconCenter},
            visor::host::PointerDown{.button = 1, .pos = kIconCenter},
            visor::host::PointerUp{.button = 1, .pos = kIconCenter},
        });
        VESPER_CHECK_EQ(s.pump(), 4);
        VESPER_CHECK_EQ(s.opened, 1);
        VESPER_CHECK_EQ(s.desktop.child_count(), std::size_t{3}); // the window joined
        VESPER_CHECK(s.strips_cover(kWinBounds));
        VESPER_CHECK(s.host.get(kWinBounds.pos)); // the border corner is lit ink
    }

    // The window stays lit on the next idle round: nothing resubmits, and
    // the pixels read the same.
    {
        Scene s;
        VESPER_CHECK(s.build());
        s.feed({
            visor::host::PointerDown{.button = 1, .pos = kIconCenter},
            visor::host::PointerUp{.button = 1, .pos = kIconCenter},
            visor::host::PointerDown{.button = 1, .pos = kIconCenter},
            visor::host::PointerUp{.button = 1, .pos = kIconCenter},
        });
        static_cast<void>(s.pump());
        s.host.clear_regions();
        VESPER_CHECK_EQ(s.pump(), 0);
        VESPER_CHECK(s.host.regions().empty()); // an idle desk submits nothing
        VESPER_CHECK(s.host.get(kWinBounds.pos));
    }

    // A slow pair opens nothing: the second click lands past the router's
    // time window (advance_ms walks the fake clock between the pumps, so
    // the two rounds read 0 ms and 600 ms). A single click alone opens
    // nothing either -- checked between the two pumps.
    {
        Scene s;
        VESPER_CHECK(s.build());
        s.feed({
            visor::host::PointerDown{.button = 1, .pos = kIconCenter},
            visor::host::PointerUp{.button = 1, .pos = kIconCenter},
        });
        VESPER_CHECK_EQ(s.pump(), 2);
        VESPER_CHECK_EQ(s.opened, 0); // one click is not the gesture
        s.host.advance_ms(600);
        s.feed({
            visor::host::PointerDown{.button = 1, .pos = kIconCenter},
            visor::host::PointerUp{.button = 1, .pos = kIconCenter},
        });
        VESPER_CHECK_EQ(s.pump(), 2);
        VESPER_CHECK_EQ(s.opened, 0);
        VESPER_CHECK_EQ(s.desktop.child_count(), std::size_t{2});
        VESPER_CHECK(!s.host.get(kWinBounds.pos)); // the ground still reads unlit
    }

    // A far pair opens nothing: both presses on the same icon, six pixels
    // apart -- past the router's slop of four per axis. The clock reads the
    // same on both rounds, so time is not the leg that breaks this pair.
    {
        Scene s;
        VESPER_CHECK(s.build());
        s.feed({
            visor::host::PointerDown{.button = 1, .pos = kIconCenter},
            visor::host::PointerUp{.button = 1, .pos = kIconCenter},
        });
        VESPER_CHECK_EQ(s.pump(), 2);
        const PointI far{kIconCenter.x + 6, kIconCenter.y};
        s.feed({
            visor::host::PointerDown{.button = 1, .pos = far},
            visor::host::PointerUp{.button = 1, .pos = far},
        });
        VESPER_CHECK_EQ(s.pump(), 2);
        VESPER_CHECK_EQ(s.opened, 0);
        VESPER_CHECK_EQ(s.desktop.child_count(), std::size_t{2});
    }

    // The bare host's full input path: first absolute report, double click,
    // capture outside the desktop, reverse, release, and close. The display
    // is larger than the desktop, as bochs-drm's preferred VM mode is.
    {
        Scene s;
        VESPER_CHECK(s.build());
        vesper::host::EvdevTranslator tablet{SizeI{.w = 1280, .h = 800},
                                             SizeI{.w = 32768, .h = 32768}};
        const auto word = [&](std::uint16_t type, std::uint16_t code, std::int32_t value) {
            if (const auto event = tablet.feed(type, code, value)) {
                VESPER_CHECK(s.host.push_event(*event));
            }
        };
        const auto move = [&](PointI at) {
            word(3, 0, ((at.x * 32767) + (1279 / 2)) / 1279);
            word(3, 1, ((at.y * 32767) + (799 / 2)) / 799);
            word(0, 0, 0);
        };
        const auto click = [&] {
            word(1, 0x110, 1);
            word(0, 0, 0);
            word(1, 0x110, 0);
            word(0, 0, 0);
            static_cast<void>(s.pump());
        };
        move(kIconCenter);
        click();
        VESPER_CHECK_EQ(s.opened, 0);
        s.host.advance_ms(100);
        click();
        VESPER_CHECK_EQ(s.opened, 1);

        move(PointI{230, 125});
        word(1, 0x110, 1);
        move(PointI{900, 600});
        static_cast<void>(s.pump());
        VESPER_CHECK_EQ(s.win.bounds().pos, (PointI{360, 240}));
        move(PointI{290, 150});
        word(1, 0x110, 0);
        static_cast<void>(s.pump());
        VESPER_CHECK_EQ(s.win.bounds().pos, (PointI{260, 145}));
        move(PointI{400, 200});
        static_cast<void>(s.pump());
        VESPER_CHECK_EQ(s.win.bounds().pos, (PointI{260, 145}));

        const RectI close = s.win.close_box();
        move(PointI{close.pos.x + (close.size.w / 2), close.pos.y + (close.size.h / 2)});
        click();
        VESPER_CHECK(!s.win.visible());
    }

    // Ink judgment: the art's corner pixel inside the icon box is lit, a
    // pixel of the blank margin around it is not. The block is art plus the
    // caption gap plus one line: 8 + 2 + 7 = 17 tall in a 32-tall box, so
    // the block starts at 20 + ((32 - 17) / 2) = 27 and the art's second row
    // is the one at y=28. The desk's ground shows between the ink.
    {
        Scene s;
        VESPER_CHECK(s.build());
        static_cast<void>(s.pump());
        // Art: 8 wide centered in a 48-wide box -> art_x = 20 + 20 = 40.
        VESPER_CHECK((s.host.get(PointI{40, 28})));  // art corner
        VESPER_CHECK((!s.host.get(PointI{39, 28}))); // one left of the art
        VESPER_CHECK((!s.host.get(PointI{20, 20}))); // the icon box's own corner
    }

    // Selection judgment: the selected look is a one-pixel ring around the
    // slot. Its corners light, the ground one pixel inside stays dark, and
    // clearing the state gives the ring's pixels back to the desk.
    {
        Scene s;
        VESPER_CHECK(s.build());
        static_cast<void>(s.pump());
        s.icon.set_selected(true);
        static_cast<void>(s.pump());
        VESPER_CHECK(s.icon.selected());
        VESPER_CHECK((s.host.get(PointI{kIconBounds.pos.x, kIconBounds.pos.y})));
        VESPER_CHECK(
            (s.host.get(PointI{kIconBounds.pos.x + kIconBounds.size.w - 1, kIconBounds.pos.y})));
        VESPER_CHECK((!s.host.get(PointI{kIconBounds.pos.x + 1, kIconBounds.pos.y + 1})));
        s.icon.set_selected(false);
        static_cast<void>(s.pump());
        VESPER_CHECK((!s.host.get(PointI{kIconBounds.pos.x, kIconBounds.pos.y})));
    }
}
