#pragma once

#include <cstdint>

#include "visor/bindings/events.hpp"

namespace vesper::host {

/**
 * @brief Folds one key event through the US text table.
 *
 * visor's evdev translator deliberately stops before text: letters and
 * digits stay evdev codes, and the text convention (printables ride their
 * code points) is left to the host that knows a layout. This is that layer
 * for the fbdev lane: a flat US table over the evdev key numbers, shift
 * folding included, one function and no state -- the shift bit comes in on
 * each event, the same place visor's modifier tracking put it.
 *
 * A key the table does not know passes its code through unchanged. The core
 * names some evdev values itself (Backspace 14, Tab 15, Enter 28, Space 57
 * and the out-of-band arrows), and those are simply absent from the table,
 * so what the core already reads survives the fold untouched; a key no
 * consumer names (a function key, a multimedia code) arrives the same way
 * and is ignored by whoever does not want it. Pointer events pass through
 * untouched.
 *
 * Keypad digits and the dot map to their ASCII values (numlock is assumed:
 * a terminal wants the numbers more than it wants the navigation codes).
 */
[[nodiscard]] visor::host::Event fold_text(visor::host::Event event) noexcept;

/// The table half, exposed for the judgments: the code point one evdev key
/// carries under one shift state, or the key's own number when the table has
/// no entry for it.
[[nodiscard]] std::uint32_t text_code_for(std::uint16_t evdev_code, bool shift) noexcept;

} // namespace vesper::host
