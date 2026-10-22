#include "cases.hpp"
#include "host/evdev_input.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <optional>
#include <variant>

#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <unistd.h>

#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/evdev/event_wire.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/events/key_words.hpp"
#include "visor/bindings/events/pointer_words.hpp"

namespace {

using vesper::host::EvdevInput;
using vesper::host::EvdevTranslator;
using visor::base::PointI;
using visor::base::SizeI;
using visor::host::Event;
using visor::host::KeyDown;
using visor::host::PointerDown;
using visor::host::PointerMove;
using visor::host::PointerUp;
using visor::host::PointerWheel;

// The raw evdev numbers these judgments feed, the same values the kernel's
// input-event-codes.h spells (visor's translator carries its own copies for
// the ones it reads; these stay local to the words under test).
constexpr std::uint16_t kEvKey = 0x01;
constexpr std::uint16_t kEvRel = 0x02;
constexpr std::uint16_t kEvAbs = 0x03;
constexpr std::uint16_t kRelX = 0x00;
constexpr std::uint16_t kRelWheel = 0x08;
constexpr std::uint16_t kAbsX = 0x00;
constexpr std::uint16_t kAbsY = 0x01;
constexpr std::uint16_t kKeyA = 30;
constexpr std::uint16_t kKeyLeftShift = 42;
constexpr std::uint16_t kKeyEnter = 28;
constexpr std::uint16_t kKeyArrowUp = 103;
constexpr std::uint16_t kBtnLeft = 0x110;

/// The judgments read what a feed produced; the read rides behind this
/// check, so an empty translation fails the test instead of the process.
/// By value: a reference into the temporary optional would dangle. The
/// guard is spelled as a plain if (not the check macro) so the optional
/// analyzer sees it; the stand-in event only exists to keep a failed test
/// running to its next check.
Event need(const std::optional<Event>& event) {
    if (event.has_value()) {
        return *event;
    }
    vesper::testing::report(__FILE__, __LINE__, "feed produced no event");
    return Event{PointerMove{}};
}

void key_presses_fold_to_text() {
    EvdevTranslator translator{SizeI{.w = 640, .h = 480}};
    const auto typed = need(translator.feed(kEvKey, kKeyA, 1));
    const KeyDown* const down = std::get_if<KeyDown>(&typed);
    VESPER_CHECK(down != nullptr);
    VESPER_CHECK_EQ(down->code, std::uint32_t{'a'});
    VESPER_CHECK(!down->modifiers.shift);
}

void held_shift_folds_into_the_event() {
    EvdevTranslator translator{SizeI{.w = 640, .h = 480}};
    const auto shift_word = need(translator.feed(kEvKey, kKeyLeftShift, 1));
    const KeyDown* const shift_down = std::get_if<KeyDown>(&shift_word);
    VESPER_CHECK(shift_down != nullptr);
    // The modifier key itself keeps its evdev number; no table entry types.
    VESPER_CHECK_EQ(shift_down->code, std::uint32_t{kKeyLeftShift});
    VESPER_CHECK(shift_down->modifiers.shift);

    const auto typed = need(translator.feed(kEvKey, kKeyA, 1));
    const KeyDown* const down = std::get_if<KeyDown>(&typed);
    VESPER_CHECK(down != nullptr);
    VESPER_CHECK_EQ(down->code, std::uint32_t{'A'});
    VESPER_CHECK(down->modifiers.shift);
}

void named_keys_survive_translation() {
    EvdevTranslator translator{SizeI{.w = 640, .h = 480}};
    const auto enter_word = need(translator.feed(kEvKey, kKeyEnter, 1));
    const KeyDown* const enter_down = std::get_if<KeyDown>(&enter_word);
    VESPER_CHECK(enter_down != nullptr);
    VESPER_CHECK_EQ(enter_down->code, visor::host::kKeyEnter);

    const auto arrow_word = need(translator.feed(kEvKey, kKeyArrowUp, 1));
    const KeyDown* const arrow_down = std::get_if<KeyDown>(&arrow_word);
    VESPER_CHECK(arrow_down != nullptr);
    VESPER_CHECK_EQ(arrow_down->code, visor::host::kKeyUp);
}

void buttons_translate_from_the_button_band() {
    EvdevTranslator translator{SizeI{.w = 640, .h = 480}};
    const auto press = need(translator.feed(kEvKey, kBtnLeft, 1));
    const PointerDown* const down = std::get_if<PointerDown>(&press);
    VESPER_CHECK(down != nullptr);
    VESPER_CHECK_EQ(down->button, std::uint32_t{1});
}

void relative_moves_accumulate_and_clamp() {
    EvdevTranslator translator{SizeI{.w = 640, .h = 480}};
    const auto first = need(translator.feed(kEvRel, kRelX, 30));
    const auto& move = std::get<PointerMove>(first);
    VESPER_CHECK_EQ(move.pos.x, 30);

    const auto far = need(translator.feed(kEvRel, kRelX, 100000));
    const auto& far_move = std::get<PointerMove>(far);
    // The screen edge stops the pointer; the move itself stays honest.
    VESPER_CHECK_EQ(far_move.pos.x, 639);
}

void wheel_steps_normalize_to_detents() {
    EvdevTranslator translator{SizeI{.w = 640, .h = 480}};
    const auto scroll = need(translator.feed(kEvRel, kRelWheel, -1));
    const auto& wheel = std::get<PointerWheel>(scroll);
    VESPER_CHECK_EQ(wheel.dy, 1);
}

PointI moved(const std::optional<Event>& event) {
    return std::get<PointerMove>(need(event)).pos;
}

void absolute_samples_keep_their_position() {
    EvdevTranslator translator{SizeI{.w = 640, .h = 480}};
    VESPER_CHECK_EQ(moved(translator.feed(kEvAbs, kAbsX, 100)).x, 100);
    VESPER_CHECK_EQ(moved(translator.feed(kEvAbs, kAbsX, 130)).x, 130);
    VESPER_CHECK_EQ(moved(translator.feed(kEvAbs, kAbsY, 200)), (PointI{130, 200}));
    VESPER_CHECK_EQ(moved(translator.feed(kEvAbs, kAbsY, 240)), (PointI{130, 240}));
    const auto press = need(translator.feed(kEvKey, kBtnLeft, 1));
    VESPER_CHECK_EQ(std::get<PointerDown>(press).pos, (PointI{130, 240}));
    VESPER_CHECK(!translator.feed(kEvAbs, kAbsY, 240).has_value());
}

void tablet_samples_use_the_full_framebuffer() {
    // The desktop occupies only the top-left 640x480 of a 1280x800 display.
    // A point over its title bar must retain the displayed pixel position.
    EvdevTranslator translator{SizeI{.w = 1280, .h = 800}, SizeI{.w = 32768, .h = 32768}};
    VESPER_CHECK_EQ(moved(translator.feed(kEvAbs, kAbsX, 8198)).x, 320);
    VESPER_CHECK_EQ(moved(translator.feed(kEvAbs, kAbsY, 2051)), (PointI{320, 50}));
    const auto press = need(translator.feed(kEvKey, kBtnLeft, 1));
    VESPER_CHECK_EQ(std::get<PointerDown>(press).pos, (PointI{320, 50}));
    // The physical display outside the desktop must not fold onto its edge.
    VESPER_CHECK_EQ(moved(translator.feed(kEvAbs, kAbsX, 24595)).x, 960);
    VESPER_CHECK_EQ(moved(translator.feed(kEvAbs, kAbsX, 32767)).x, 1279);
    VESPER_CHECK_EQ(moved(translator.feed(kEvAbs, kAbsY, 32767)).y, 799);
}

void absolute_reversal_and_relative_moves_do_not_drift() {
    EvdevTranslator translator{SizeI{.w = 640, .h = 480}};
    VESPER_CHECK_EQ(moved(translator.feed(kEvAbs, kAbsX, 700)).x, 639);
    VESPER_CHECK_EQ(moved(translator.feed(kEvAbs, kAbsX, 638)).x, 638);
    VESPER_CHECK_EQ(moved(translator.feed(kEvRel, kRelX, -100)).x, 538);
    VESPER_CHECK_EQ(moved(translator.feed(kEvAbs, kAbsX, 638)).x, 638);
    VESPER_CHECK_EQ(moved(translator.feed(kEvAbs, kAbsX, -100)).x, 0);
    VESPER_CHECK_EQ(moved(translator.feed(kEvAbs, kAbsX, 1)).x, 1);
}

void absolute_samples_clamp_without_overflow() {
    EvdevTranslator translator{SizeI{.w = 640, .h = 480}, SizeI{.w = 32768, .h = 32768}};
    VESPER_CHECK_EQ(moved(translator.feed(kEvAbs, kAbsX, 32767)).x, 639);
    VESPER_CHECK_EQ(
        moved(translator.feed(kEvAbs, kAbsX, std::numeric_limits<std::int32_t>::min())).x, 0);
    VESPER_CHECK_EQ(
        moved(translator.feed(kEvAbs, kAbsX, std::numeric_limits<std::int32_t>::max())).x, 639);
}

void tablet_ranges_can_start_below_zero() {
    EvdevTranslator translator{SizeI{.w = 640, .h = 480}, SizeI{.w = 2001, .h = 1001},
                               PointI{-1000, 100}};
    VESPER_CHECK_EQ(moved(translator.feed(kEvAbs, kAbsX, 0)).x, 320);
    VESPER_CHECK_EQ(moved(translator.feed(kEvAbs, kAbsY, 600)).y, 240);
    VESPER_CHECK_EQ(moved(translator.feed(kEvAbs, kAbsX, -1000)).x, 0);
    VESPER_CHECK_EQ(moved(translator.feed(kEvAbs, kAbsY, 1100)).y, 479);
    VESPER_CHECK_EQ(moved(translator.feed(kEvAbs, kAbsX, 1000)).x, 639);
}

void tiny_displays_and_invalid_ranges_stay_bounded() {
    EvdevTranslator empty{SizeI{.w = 0, .h = 0}, SizeI{.w = 32768, .h = 32768}};
    VESPER_CHECK(!empty.feed(kEvAbs, kAbsX, 32767).has_value());
    VESPER_CHECK(!empty.feed(kEvAbs, kAbsY, 32767).has_value());
    EvdevTranslator pixel{SizeI{.w = 1, .h = 1}, SizeI{.w = 32768, .h = 32768}};
    VESPER_CHECK(!pixel.feed(kEvAbs, kAbsX, 32767).has_value());
    VESPER_CHECK(!pixel.feed(kEvAbs, kAbsY, 32767).has_value());
    EvdevTranslator fallback{SizeI{.w = 640, .h = 480}, SizeI{.w = 0, .h = 1}};
    VESPER_CHECK_EQ(moved(fallback.feed(kEvAbs, kAbsX, 300)).x, 300);
    VESPER_CHECK_EQ(moved(fallback.feed(kEvAbs, kAbsY, 900)).y, 479);
}

void syn_dropped_rebuilds_the_stream_state() {
    EvdevTranslator translator{SizeI{.w = 640, .h = 480}};
    static_cast<void>(translator.feed(kEvKey, kKeyLeftShift, 1));
    static_cast<void>(translator.feed(kEvAbs, kAbsX, 100));
    VESPER_CHECK(!translator.feed(0x00, 0x03, 0).has_value());
    // The next absolute sample restores the actual position immediately.
    VESPER_CHECK_EQ(moved(translator.feed(kEvAbs, kAbsX, 100)).x, 100);
    const auto typed = need(translator.feed(kEvKey, kKeyA, 1));
    VESPER_CHECK_EQ(std::get<KeyDown>(typed).code, std::uint32_t{'a'});
    VESPER_CHECK(!std::get<KeyDown>(typed).modifiers.shift);
}

/// A nonblocking evdev-shaped stream without a kernel input device.
/// Keeping the writer open lets an incomplete report end in EAGAIN.
struct InputPipe {
    std::array<int, 2> fds{-1, -1};
    EvdevInput input;

    ~InputPipe() {
        for (const int fd : fds) {
            if (fd >= 0) {
                static_cast<void>(::close(fd));
            }
        }
    }

    bool open() {
        if (::pipe(fds.data()) != 0) {
            return false;
        }
        std::array<char, 64> path{};
        static_cast<void>(std::snprintf(path.data(), path.size(), "/proc/self/fd/%d", fds[0]));
        return input.open(path.data(), SizeI{.w = 1280, .h = 800});
    }

    void send(std::initializer_list<visor::host::evdev::WireEvent> words) const {
        for (const auto& raw : words) {
            input_event word{};
            word.type = raw.type;
            word.code = raw.code;
            word.value = raw.value;
            VESPER_CHECK_EQ(static_cast<std::size_t>(::write(fds[1], &word, sizeof(word))),
                            sizeof(word));
        }
    }
};

void button_reports_use_their_final_coordinates() {
    InputPipe stream;
    if (!stream.open()) {
        VESPER_CHECK(false);
        return;
    }
    // A real QEMU tablet report can name the button before its coordinates.
    // Split the report at EAGAIN too: the button must wait for SYN_REPORT.
    stream.send({
        {.type = EV_KEY, .code = BTN_LEFT, .value = 1},
        {.type = EV_ABS, .code = ABS_X, .value = 300},
        {.type = EV_ABS, .code = ABS_Y, .value = 45},
    });
    Event event;
    VESPER_CHECK(stream.input.poll_next(event));
    VESPER_CHECK(std::holds_alternative<PointerMove>(event));
    VESPER_CHECK(stream.input.poll_next(event));
    VESPER_CHECK(std::holds_alternative<PointerMove>(event));
    VESPER_CHECK(!stream.input.poll_next(event));
    stream.send({{.type = EV_SYN, .code = SYN_REPORT, .value = 0}});
    VESPER_CHECK(stream.input.poll_next(event));
    const auto* down = std::get_if<PointerDown>(&event);
    VESPER_CHECK(down != nullptr);
    if (down != nullptr) {
        VESPER_CHECK_EQ(down->pos, (PointI{300, 45}));
    }
    VESPER_CHECK(!stream.input.poll_next(event));

    // Release coordinates also belong to this report, so the captured drag
    // receives its final position before the release stops it.
    stream.send({
        {.type = EV_KEY, .code = BTN_LEFT, .value = 0},
        {.type = EV_ABS, .code = ABS_X, .value = 320},
        {.type = EV_ABS, .code = ABS_Y, .value = 80},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
    });
    VESPER_CHECK(stream.input.poll_next(event));
    VESPER_CHECK(std::holds_alternative<PointerMove>(event));
    VESPER_CHECK(stream.input.poll_next(event));
    VESPER_CHECK(std::holds_alternative<PointerMove>(event));
    VESPER_CHECK(stream.input.poll_next(event));
    const auto* up = std::get_if<PointerUp>(&event);
    VESPER_CHECK(up != nullptr);
    if (up != nullptr) {
        VESPER_CHECK_EQ(up->pos, (PointI{320, 80}));
    }
    VESPER_CHECK(!stream.input.poll_next(event));
}

void reports_preserve_all_buttons_and_discard_dropped_ones() {
    InputPipe stream;
    if (!stream.open()) {
        VESPER_CHECK(false);
        return;
    }
    stream.send({
        {.type = EV_KEY, .code = BTN_LEFT, .value = 1},
        {.type = EV_KEY, .code = BTN_RIGHT, .value = 1},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
        {.type = EV_KEY, .code = BTN_LEFT, .value = 0},
        {.type = EV_SYN, .code = SYN_DROPPED, .value = 0},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
    });
    Event event;
    for (const std::uint32_t button : {1U, 2U}) {
        VESPER_CHECK(stream.input.poll_next(event));
        const auto* down = std::get_if<PointerDown>(&event);
        VESPER_CHECK(down != nullptr);
        if (down != nullptr) {
            VESPER_CHECK_EQ(down->button, button);
        }
    }
    VESPER_CHECK(!stream.input.poll_next(event));
}

void idle_words_translate_to_nothing() {
    EvdevTranslator translator{SizeI{.w = 640, .h = 480}};
    // A plain SYN_REPORT, an unknown event type, a key value out of range.
    VESPER_CHECK(!translator.feed(0x00, 0x00, 0).has_value());
    VESPER_CHECK(!translator.feed(0x05, 0x00, 1).has_value());
    VESPER_CHECK(!translator.feed(kEvKey, kKeyA, 9).has_value());
}

} // namespace

void run_evdev_input_tests() {
    key_presses_fold_to_text();
    held_shift_folds_into_the_event();
    named_keys_survive_translation();
    buttons_translate_from_the_button_band();
    relative_moves_accumulate_and_clamp();
    wheel_steps_normalize_to_detents();
    absolute_samples_keep_their_position();
    tablet_samples_use_the_full_framebuffer();
    absolute_reversal_and_relative_moves_do_not_drift();
    absolute_samples_clamp_without_overflow();
    tablet_ranges_can_start_below_zero();
    tiny_displays_and_invalid_ranges_stay_bounded();
    syn_dropped_rebuilds_the_stream_state();
    button_reports_use_their_final_coordinates();
    reports_preserve_all_buttons_and_discard_dropped_ones();
    idle_words_translate_to_nothing();
}
