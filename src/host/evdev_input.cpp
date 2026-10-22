#include "host/evdev_input.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <variant>

#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "host/keymap.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/evdev/event_wire.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/events/pointer_words.hpp"

namespace vesper::host {
namespace {

// Event types and codes this layer reads beyond visor's vocabulary. The
// numbers are linux/input-event-codes.h's, spelled here so the translator
// stays free of kernel headers exactly as its own header promises.
constexpr std::uint16_t kEvAbs = 0x03;
constexpr std::uint16_t kAbsX = 0x00;
constexpr std::uint16_t kAbsY = 0x01;
constexpr std::uint16_t kSynDropped = 0x03;

/// Clamp before scaling, and map inclusive axis endpoints to inclusive
/// display endpoints. All arithmetic is widened before subtraction so even
/// a malformed INT32_MIN sample cannot overflow the host adapter.
std::int32_t scale_to_screen(std::int32_t sample, int screen_extent, std::int32_t tablet_extent,
                             int minimum) noexcept {
    if (screen_extent <= 0) {
        return 0;
    }
    const auto last_pixel = static_cast<std::int64_t>(screen_extent) - 1;
    const auto offset = static_cast<std::int64_t>(sample) - minimum;
    if (tablet_extent <= 1) {
        return static_cast<std::int32_t>(std::clamp(offset, std::int64_t{0}, last_pixel));
    }
    const auto last_unit = static_cast<std::int64_t>(tablet_extent) - 1;
    const auto clamped = std::clamp(offset, std::int64_t{0}, last_unit);
    return static_cast<std::int32_t>(((clamped * last_pixel) + (last_unit / 2)) / last_unit);
}

/// Invalid or unrepresentable ranges keep the caller's 1:1 fallback.
bool read_axis(int fd, std::uint16_t axis, input_absinfo& info) noexcept {
    if (::ioctl(fd, EVIOCGABS(axis), &info) != 0) {
        return false;
    }
    const auto extent = static_cast<std::int64_t>(info.maximum) - info.minimum + 1;
    return extent > 1 && extent <= std::numeric_limits<int>::max();
}

} // namespace

EvdevTranslator::EvdevTranslator(visor::base::SizeI screen) noexcept
    : EvdevTranslator{screen, screen} {}

EvdevTranslator::EvdevTranslator(visor::base::SizeI screen, visor::base::SizeI tablet_extent,
                                 visor::base::PointI tablet_minimum) noexcept
    : screen_{screen}, tablet_extent_{tablet_extent}, tablet_minimum_{tablet_minimum},
      state_{screen} {}

std::optional<visor::host::Event> EvdevTranslator::feed(std::uint16_t type, std::uint16_t code,
                                                        std::int32_t value) noexcept {
    if (type == kEvAbs && (code == kAbsX || code == kAbsY)) {
        const bool is_x = code == kAbsX;
        const std::int32_t pixel =
            is_x ? scale_to_screen(value, screen_.w, tablet_extent_.w, tablet_minimum_.x)
                 : scale_to_screen(value, screen_.h, tablet_extent_.h, tablet_minimum_.y);
        const int current = is_x ? pointer_.x : pointer_.y;
        if (current == pixel) {
            return std::nullopt;
        }
        return translate(visor::host::evdev::WireEvent{
            .type = visor::host::evdev::kEvRel,
            .code = is_x ? visor::host::evdev::kRelX : visor::host::evdev::kRelY,
            .value = pixel - current,
        });
    }
    if (type == visor::host::evdev::kEvSyn && code == kSynDropped) {
        reset();
        return std::nullopt;
    }
    return translate(visor::host::evdev::WireEvent{.type = type, .code = code, .value = value});
}

std::optional<visor::host::Event>
EvdevTranslator::translate(const visor::host::evdev::WireEvent& word) noexcept {
    const std::optional<visor::host::Event> event =
        visor::host::evdev::translate_event(state_, word);
    if (!event.has_value()) {
        return std::nullopt;
    }
    if (const auto* move = std::get_if<visor::host::PointerMove>(&*event)) {
        pointer_ = move->pos;
    }
    return fold_text(*event);
}

void EvdevTranslator::reset() noexcept {
    pointer_ = visor::base::PointI{};
    state_ = visor::host::evdev::EvdevState{screen_};
}

EvdevInput::~EvdevInput() {
    close();
}

bool EvdevInput::open(const char* path, visor::base::SizeI screen) noexcept {
    close();
    fd_ = ::open(path, O_RDONLY | O_NONBLOCK);
    if (fd_ < 0) {
        return false;
    }
    // Grab the device so the console tty does not also receive the input.
    // Best-effort: an already-grabbed device still reads.
    static_cast<void>(::ioctl(fd_, EVIOCGRAB, 1));
    // A tablet spans its own axis range over the display (QEMU's usb-tablet
    // reports 0..32767), so the fold scales by the real extent; a device
    // whose axes do not answer keeps the 1:1 fallback.
    visor::base::SizeI extent = screen;
    visor::base::PointI minimum{};
    input_absinfo x_info{};
    const bool has_x = read_axis(fd_, kAbsX, x_info);
    if (has_x) {
        extent.w = static_cast<int>(static_cast<std::int64_t>(x_info.maximum) - x_info.minimum + 1);
        minimum.x = x_info.minimum;
    }
    input_absinfo y_info{};
    const bool has_y = read_axis(fd_, kAbsY, y_info);
    if (has_y) {
        extent.h = static_cast<int>(static_cast<std::int64_t>(y_info.maximum) - y_info.minimum + 1);
        minimum.y = y_info.minimum;
    }
    translator_ = EvdevTranslator{screen, extent, minimum};
    // The kernel need not repeat an unchanged axis after open. Seed both
    // positions so a stationary click already has the device's coordinates.
    if (has_x) {
        static_cast<void>(translator_.feed(kEvAbs, kAbsX, x_info.value));
    }
    if (has_y) {
        static_cast<void>(translator_.feed(kEvAbs, kAbsY, y_info.value));
    }
    return true;
}

void EvdevInput::close() noexcept {
    buttons_.clear();
    next_button_ = 0;
    report_ready_ = false;
    if (fd_ >= 0) {
        static_cast<void>(::ioctl(fd_, EVIOCGRAB, 0));
        static_cast<void>(::close(fd_));
        fd_ = -1;
    }
}

bool EvdevInput::poll_next(visor::host::Event& out) noexcept {
    if (fd_ < 0) {
        return false;
    }
    // Drain until one word produces an event; EAGAIN and partial reads end
    // the call with the partial state kept for the next one.
    for (;;) {
        if (report_ready_) {
            while (next_button_ < buttons_.size()) {
                const auto word = buttons_[next_button_++];
                if (const auto event = translator_.feed(word.type, word.code, word.value)) {
                    out = *event;
                    return true;
                }
            }
            buttons_.clear();
            next_button_ = 0;
            report_ready_ = false;
        }
        input_event word{};
        const auto got = static_cast<std::size_t>(::read(fd_, &word, sizeof(word)));
        if (got != sizeof(word)) {
            return false;
        }
        if (word.type == EV_KEY &&
            (word.code == BTN_LEFT || word.code == BTN_RIGHT || word.code == BTN_MIDDLE)) {
            buttons_.push_back(visor::host::evdev::WireEvent{
                .type = word.type,
                .code = word.code,
                .value = word.value,
            });
            continue;
        }
        if (word.type == EV_SYN && word.code == SYN_REPORT) {
            report_ready_ = true;
            continue;
        }
        if (word.type == EV_SYN && word.code == SYN_DROPPED) {
            buttons_.clear();
        }
        const std::optional<visor::host::Event> event =
            translator_.feed(static_cast<std::uint16_t>(word.type),
                             static_cast<std::uint16_t>(word.code), word.value);
        if (event.has_value()) {
            out = *event;
            return true;
        }
    }
}

} // namespace vesper::host
