#include "cases.hpp"
#include "testing.hpp"
#include "ui/icon/icon.hpp"
#include "ui/launcher/launcher.hpp"
#include "ui/style/style.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_bitmap.hpp"

#include <array>
#include <cstddef>
#include <string_view>

namespace {

using vesper::desktop::Icon;
using vesper::desktop::LaunchEntry;
using vesper::desktop::Launcher;

const visor::draw::MonoBitmap kArt{};

void run_table_tests() {
    // The words ride unchanged: caption, art, path and argv are the
    // program's whole identity to the desk, and the registry's one job is
    // to hand them back exactly as they arrived.
    const std::array<Icon::Layer, 2> layers{
        Icon::Layer{.art = &kArt, .ink = visor::canvas::ColorRole::Muted},
        Icon::Layer{.art = &kArt, .ink = visor::canvas::ColorRole::Accent},
    };
    const std::array<const char*, 2> argv{"/bin/example", "--flag"};

    const visor::base::RectI column_top{
        .pos = visor::base::PointI{16, 16},
        .size =
            visor::base::SizeI{
                .w = vesper::desktop::style::kIconSlotWidth,
                .h = vesper::desktop::style::kIconSlotHeight,
            },
    };
    Launcher launcher{column_top};
    VESPER_CHECK(launcher.add(
        {.caption = "EXAMPLE", .layers = layers, .path = "/bin/example", .argv = argv}));
    VESPER_CHECK_EQ(launcher.count(), 1U);
    const LaunchEntry* entry = launcher.at(0);
    VESPER_CHECK(entry != nullptr);
    if (entry != nullptr) {
        VESPER_CHECK_EQ(entry->caption, std::string_view{"EXAMPLE"});
        VESPER_CHECK_EQ(std::string_view{entry->path}, std::string_view{"/bin/example"});
        VESPER_CHECK_EQ(entry->argv.size(), std::size_t{2});
        VESPER_CHECK_EQ(std::string_view{entry->argv[1]}, std::string_view{"--flag"});
        VESPER_CHECK_EQ(entry->layers.size(), std::size_t{2});
    }

    // Past the count answers null, the quiet end of the column.
    VESPER_CHECK(launcher.at(1) == nullptr);

    // An entry that names no path is refused before anything changes.
    VESPER_CHECK(
        !launcher.add({.caption = "NOWHERE", .layers = layers, .path = nullptr, .argv = {}}));
    VESPER_CHECK_EQ(launcher.count(), 1U);

    // The table fills to capacity and refuses the next add whole: the
    // earlier words keep their places, the newcomer keeps none.
    for (std::size_t i = launcher.count(); i < Launcher::kCapacity; ++i) {
        VESPER_CHECK(
            launcher.add({.caption = "FILLER", .layers = {}, .path = "/bin/filler", .argv = {}}));
    }
    VESPER_CHECK_EQ(launcher.count(), Launcher::kCapacity);
    VESPER_CHECK(
        !launcher.add({.caption = "NINTH", .layers = {}, .path = "/bin/ninth", .argv = {}}));
    VESPER_CHECK_EQ(launcher.count(), Launcher::kCapacity);
    VESPER_CHECK_EQ(launcher.at(0)->caption, std::string_view{"EXAMPLE"});
    VESPER_CHECK_EQ(launcher.at(Launcher::kCapacity - 1)->caption, std::string_view{"FILLER"});
}

void run_column_tests() {
    // The column's step is the style's own: one slot height plus the
    // caption's air, straight down from the first slot.
    const visor::base::RectI first{
        .pos = visor::base::PointI{16, 16},
        .size =
            visor::base::SizeI{
                .w = vesper::desktop::style::kIconSlotWidth,
                .h = vesper::desktop::style::kIconSlotHeight,
            },
    };
    Launcher launcher{first};
    const visor::base::RectI second = launcher.slot_for(1);
    const visor::base::RectI third = launcher.slot_for(2);
    VESPER_CHECK(launcher.slot_for(0).pos.x == first.pos.x);
    VESPER_CHECK(launcher.slot_for(0).pos.y == first.pos.y);
    VESPER_CHECK_EQ(second.pos.x, first.pos.x);
    VESPER_CHECK_EQ(second.pos.y, first.pos.y + vesper::desktop::style::kIconSlotHeight +
                                      vesper::desktop::style::kCaptionGap);
    VESPER_CHECK_EQ(third.pos.y, second.pos.y + vesper::desktop::style::kIconSlotHeight +
                                     vesper::desktop::style::kCaptionGap);
    VESPER_CHECK_EQ(second.size.w, first.size.w);
    VESPER_CHECK_EQ(second.size.h, first.size.h);
}

} // namespace

void run_launcher_tests() {
    run_table_tests();
    run_column_tests();
}
