#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/evdev/event_wire.hpp"
#include "visor/bindings/events.hpp"

namespace vesper::host {

/**
 * @brief Pure translation from an evdev word stream to core events.
 *
 * Adapted from Cinux-GUI@0b0c135 host/evdev_device.{hpp,cpp}: the read-loop
 * shape, the EVIOCGRAB stance and the SYN_DROPPED rebuild are that repo's;
 * the accumulator itself retired in favor of visor's translator, with two
 * halves added here that visor deliberately leaves to the host.
 *
 * First, the absolute fold: usb-tablet samples name positions over the
 * entire physical display, including any area outside the desktop canvas.
 * Scale the reported axis range into that display, then feed the difference
 * from the current pointer to visor's relative translator. The first sample
 * places the pointer too; treating it as a baseline would offset every click.
 * Differences use the actual clamped pointer, so reversals at an edge and
 * streams mixing absolute and relative words cannot accumulate drift.
 *
 * Second, the text fold: key events pass through the US keymap so
 * printables ride their code points, the text-host convention the terminal
 * already reads.
 *
 * One translator lives per device -- the keyboard's state carries the
 * modifiers, the pointer's carries the position, and merging the two
 * devices into one state would let a tablet word clobber a held shift.
 */
class EvdevTranslator {
  public:
    /// Tablet units are screen pixels, the 1:1 construction a device
    /// without readable axes falls back to (and the judgments use).
    explicit EvdevTranslator(visor::base::SizeI screen) noexcept;

    /// @p screen is the physical display, not the desktop canvas. Absolute
    /// axes span @p tablet_extent units starting at @p tablet_minimum;
    /// their inclusive endpoints map to the display's first and last pixels.
    EvdevTranslator(visor::base::SizeI screen, visor::base::SizeI tablet_extent,
                    visor::base::PointI tablet_minimum = {}) noexcept;

    /// Feeds one word (type, code, value as Linux reports them). Most words
    /// translate to nothing; those that do carry the folded keymap.
    [[nodiscard]] std::optional<visor::host::Event> feed(std::uint16_t type, std::uint16_t code,
                                                         std::int32_t value) noexcept;

    /// Drops everything the stream ever said: the tracked pointer and held
    /// modifiers. The word to spend this on is
    /// SYN_DROPPED, where the kernel lost events and every state above may
    /// be stale -- rebuilding beats believing.
    void reset() noexcept;

  private:
    /// The screen the rebuilt state bounds its pointer to, and the axis
    /// extent absolute samples are scaled by.
    visor::base::SizeI screen_;
    visor::base::SizeI tablet_extent_;
    visor::base::PointI tablet_minimum_;
    /// Mirrors the translator's emitted moves so absolute words can target
    /// its actual position without accessing visor's private accumulator.
    visor::base::PointI pointer_;

    [[nodiscard]] std::optional<visor::host::Event>
    translate(const visor::host::evdev::WireEvent& word) noexcept;

    visor::host::evdev::EvdevState state_;
};

/**
 * @brief Owning handle on one Linux evdev device node (/dev/input/eventN).
 *
 * open() grabs the device (EVIOCGRAB) so the kernel console does not also
 * receive the input -- best-effort, an already-grabbed device still reads.
 * poll_next() drains pending words through the translator and returns the
 * first event one of them produces; false when nothing waits, leaving
 * partial state for the next call. Pointer buttons wait for SYN_REPORT:
 * Linux may place them before the coordinates in the same tablet report,
 * and hit testing must use the report's final position.
 */
class EvdevInput {
  public:
    EvdevInput() = default;
    ~EvdevInput();

    EvdevInput(const EvdevInput&) = delete;
    EvdevInput& operator=(const EvdevInput&) = delete;

    /// Opens @p path non-blocking; @p screen bounds the tracked pointer and
    /// scales a tablet's absolute samples (EVIOCGABS reads the axis range;
    /// a device without readable axes falls back to 1:1).
    bool open(const char* path, visor::base::SizeI screen) noexcept;

    void close() noexcept;

    [[nodiscard]] bool ready() const noexcept { return fd_ >= 0; }
    [[nodiscard]] int fd() const noexcept { return fd_; }

    /// Drains pending input and hands out one translated event. Non-blocking;
    /// false when no complete word produced anything.
    [[nodiscard]] bool poll_next(visor::host::Event& out) noexcept;

  private:
    int fd_ = -1;
    EvdevTranslator translator_{visor::base::SizeI{}};
    std::vector<visor::host::evdev::WireEvent> buttons_;
    std::size_t next_button_ = 0;
    bool report_ready_ = false;
};

} // namespace vesper::host
