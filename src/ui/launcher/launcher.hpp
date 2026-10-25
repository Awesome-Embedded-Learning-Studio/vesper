#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

#include "ui/icon/icon.hpp"
#include "visor/base/geometry/rect.hpp"

namespace vesper::desktop {

/**
 * @brief One launchable program's words (the roadmap's L1): what the icon
 *        shows and what the fourth slot births.
 *
 * The entry is data, not code. Caption and layers name the program on the
 * desk; path and argv name it to exec, argv[0] included the way the spawn
 * request carries it, and an empty argv means path alone (argv[0] naming
 * the program). Every string and bitmap is borrowed by pointer: a desk's
 * programs are as static as its fonts, and the registry never copies.
 */
struct LaunchEntry {
    std::string_view caption;
    std::span<const Icon::Layer> layers;
    const char* path = nullptr;
    std::span<const char* const> argv;
};

/**
 * @brief The desk's book of launchable programs, in column order.
 *
 * The registry is the launcher's whole trick: a desk fills it once from
 * static data, lays one icon per entry down the column, and a double click
 * births whatever the entry names -- a new program is a new table row,
 * never new desk code. The geometry half lives here too: slot_for steps
 * the column by the icon style's own measures, so the placement is judged
 * beside the words instead of hand-written in every desk that hosts the
 * column. A fixed capacity, the stance every book on this desk takes: a
 * fuller column gets a refused add, not a quiet overwrite.
 */
class Launcher final {
  public:
    static constexpr std::size_t kCapacity = 8;

    /// first_slot is the column's top slot; later slots step down by the
    /// style's slot height plus caption gap.
    explicit Launcher(visor::base::RectI first_slot) noexcept;

    /// One program joins the column. False when the table is full or the
    /// entry names no path; a refused add changes nothing.
    [[nodiscard]] bool add(const LaunchEntry& entry) noexcept;

    /// Live entries, in column order.
    [[nodiscard]] std::size_t count() const noexcept { return count_; }

    /// One entry's words, or null past the count.
    [[nodiscard]] const LaunchEntry* at(std::size_t index) const noexcept;

    /// The slot the index-th icon occupies. An index past the capacity
    /// still answers a slot (the step keeps counting), because the caller
    /// that asks has already lost to a refused add somewhere else.
    [[nodiscard]] visor::base::RectI slot_for(std::size_t index) const noexcept;

  private:
    visor::base::RectI first_slot_;
    std::array<LaunchEntry, kCapacity> entries_{};
    std::size_t count_ = 0;
};

} // namespace vesper::desktop
