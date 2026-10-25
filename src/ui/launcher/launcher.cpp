#include "ui/launcher/launcher.hpp"

#include <cstddef>

#include "ui/style/style.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"

namespace vesper::desktop {

Launcher::Launcher(visor::base::RectI first_slot) noexcept : first_slot_{first_slot} {}

bool Launcher::add(const LaunchEntry& entry) noexcept {
    if (count_ == kCapacity || entry.path == nullptr) {
        return false;
    }
    entries_[count_] = entry;
    ++count_;
    return true;
}

const LaunchEntry* Launcher::at(std::size_t index) const noexcept {
    if (index >= count_) {
        return nullptr;
    }
    return &entries_[index];
}

visor::base::RectI Launcher::slot_for(std::size_t index) const noexcept {
    // The column's own step: one slot's height plus the caption's air,
    // walked down from the first slot's top-left.
    const int step = style::kIconSlotHeight + style::kCaptionGap;
    return visor::base::RectI{
        .pos = visor::base::PointI{first_slot_.pos.x,
                                   first_slot_.pos.y + (static_cast<int>(index) * step)},
        .size = first_slot_.size,
    };
}

} // namespace vesper::desktop
