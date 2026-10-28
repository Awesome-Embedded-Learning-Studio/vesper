#include "cases.hpp"
#include "host/keymap.hpp"

#include <cstdint>
#include <initializer_list>

#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/events/key_words.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/bindings/events/pointer_words.hpp"

namespace {

using vesper::host::fold_text;
using visor::base::PointI;
using visor::host::Event;
using visor::host::KeyDown;
using visor::host::KeyUp;
using visor::host::Modifiers;
using visor::host::PointerMove;

void letters_fold_with_and_without_shift() {
    VESPER_CHECK_EQ(vesper::host::text_code_for(30, false), std::uint32_t{'a'});
    VESPER_CHECK_EQ(vesper::host::text_code_for(30, true), std::uint32_t{'A'});
    VESPER_CHECK_EQ(vesper::host::text_code_for(44, false), std::uint32_t{'z'});
    VESPER_CHECK_EQ(vesper::host::text_code_for(44, true), std::uint32_t{'Z'});
}

void digits_and_punctuation_fold() {
    VESPER_CHECK_EQ(vesper::host::text_code_for(2, false), std::uint32_t{'1'});
    VESPER_CHECK_EQ(vesper::host::text_code_for(2, true), std::uint32_t{'!'});
    VESPER_CHECK_EQ(vesper::host::text_code_for(11, false), std::uint32_t{'0'});
    VESPER_CHECK_EQ(vesper::host::text_code_for(11, true), std::uint32_t{')'});
    VESPER_CHECK_EQ(vesper::host::text_code_for(12, false), std::uint32_t{'-'});
    VESPER_CHECK_EQ(vesper::host::text_code_for(12, true), std::uint32_t{'_'});
    VESPER_CHECK_EQ(vesper::host::text_code_for(53, true), std::uint32_t{'?'});
    VESPER_CHECK_EQ(vesper::host::text_code_for(41, true), std::uint32_t{'~'});
    VESPER_CHECK_EQ(vesper::host::text_code_for(43, true), std::uint32_t{'|'});
}

void keypad_digits_fold() {
    VESPER_CHECK_EQ(vesper::host::text_code_for(71, false), std::uint32_t{'7'});
    VESPER_CHECK_EQ(vesper::host::text_code_for(75, false), std::uint32_t{'4'});
    VESPER_CHECK_EQ(vesper::host::text_code_for(79, false), std::uint32_t{'1'});
    VESPER_CHECK_EQ(vesper::host::text_code_for(82, false), std::uint32_t{'0'});
    VESPER_CHECK_EQ(vesper::host::text_code_for(83, false), std::uint32_t{'.'});
}

void named_and_unknown_keys_pass_through() {
    // The core's named evdev values survive the fold untouched, the arrows'
    // out-of-band codes included. Opaque keys must not collide with text.
    VESPER_CHECK_EQ(vesper::host::text_code_for(14, false), std::uint32_t{14});
    VESPER_CHECK_EQ(vesper::host::text_code_for(15, false), std::uint32_t{15});
    VESPER_CHECK_EQ(vesper::host::text_code_for(28, false), std::uint32_t{28});
    VESPER_CHECK_EQ(vesper::host::text_code_for(57, false), std::uint32_t{57});
    VESPER_CHECK_EQ(vesper::host::text_code_for(0x110, false), std::uint32_t{0x110});
    VESPER_CHECK_EQ(vesper::host::text_code_for(59, false), std::uint32_t{0x1003b});
}

void modifiers_do_not_type_printable_bytes() {
    for (const std::uint16_t code : {42, 54, 56, 58, 69, 70, 97, 100, 125, 126}) {
        VESPER_CHECK(vesper::host::text_code_for(code, true) > 126);
    }
    VESPER_CHECK_EQ(vesper::host::text_code_for(5, true), std::uint32_t{'$'});
    VESPER_CHECK_EQ(vesper::host::text_code_for(53, true), std::uint32_t{'?'});
}

void fold_text_rewrites_key_events_only() {
    const KeyDown typed = std::get<KeyDown>(
        fold_text(Event{KeyDown{.code = 30, .modifiers = Modifiers{.shift = false}}}));
    VESPER_CHECK_EQ(typed.code, std::uint32_t{'a'});

    const KeyDown shifted = std::get<KeyDown>(
        fold_text(Event{KeyDown{.code = 30, .modifiers = Modifiers{.shift = true}}}));
    VESPER_CHECK_EQ(shifted.code, std::uint32_t{'A'});
    VESPER_CHECK(shifted.modifiers.shift);

    const KeyUp released =
        std::get<KeyUp>(fold_text(Event{KeyUp{.code = 51, .modifiers = Modifiers{.shift = true}}}));
    VESPER_CHECK_EQ(released.code, std::uint32_t{'<'});

    // Space arrives as the evdev 57 and leaves spelled 0x20, so the text
    // world keeps '9' and space apart.
    const KeyDown spaced =
        std::get<KeyDown>(fold_text(Event{KeyDown{.code = 57, .modifiers = Modifiers{}}}));
    VESPER_CHECK_EQ(spaced.code, 0x20U);

    const PointerMove move =
        std::get<PointerMove>(fold_text(Event{PointerMove{.pos = PointI{3, 4}}}));
    VESPER_CHECK_EQ(move.pos.x, 3);
    VESPER_CHECK_EQ(move.pos.y, 4);
}

void inverse_walk_returns_the_wire_numbers() {
    // Letters, digits and both spellings of a shifted key go back to the
    // evdev numbers the wire speaks.
    VESPER_CHECK_EQ(vesper::host::evdev_code_for('a'), 30U);
    VESPER_CHECK_EQ(vesper::host::evdev_code_for('Z'), 44U);
    VESPER_CHECK_EQ(vesper::host::evdev_code_for('!'), 2U);
    VESPER_CHECK_EQ(vesper::host::evdev_code_for('?'), 53U);

    // A digit typed on both blocks takes the main block's key: the wire's
    // xkb table types the same character from either, so the fold just
    // picks the first.
    VESPER_CHECK_EQ(vesper::host::evdev_code_for('7'), 8U);

    // Space folds back first: the text world spells it 0x20, the wire
    // speaks 57, and the digit '9' owns 57 as a code point.
    VESPER_CHECK_EQ(vesper::host::evdev_code_for(0x20), 57U);
    VESPER_CHECK_EQ(vesper::host::evdev_code_for('9'), 10U);
    VESPER_CHECK_EQ(vesper::host::evdev_code_for('g'), 34U);

    // The core's named keys and everything else the table does not type
    // come back as zero -- the caller passes them through.
    VESPER_CHECK_EQ(vesper::host::evdev_code_for(14), 0U);
    VESPER_CHECK_EQ(vesper::host::evdev_code_for(28), 0U);
    VESPER_CHECK_EQ(vesper::host::evdev_code_for(0x110), 0U);
}

} // namespace

void run_keymap_tests() {
    letters_fold_with_and_without_shift();
    digits_and_punctuation_fold();
    keypad_digits_fold();
    named_and_unknown_keys_pass_through();
    modifiers_do_not_type_printable_bytes();
    fold_text_rewrites_key_events_only();
    inverse_walk_returns_the_wire_numbers();
}
