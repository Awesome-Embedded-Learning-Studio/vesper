#pragma once

#include <cstdint>

#include "visor/bindings/events.hpp"
#include "visor/bindings/host.hpp"

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
 * Space is folded here too: the table passes the evdev 57 through, but 57
 * doubles as the digit '9' in the text world, so the fold spells space
 * 0x20 -- the same reading the X11 and fbdev hosts already hand down, and
 * the one the terminal's byte table expects. A key the table does not know
 * passes its code through unchanged. The core names some evdev values
 * itself (Backspace 14, Tab 15, Enter 28 and the out-of-band arrows), and
 * those are simply absent from the table, so what the core already reads
 * survives the fold untouched; a key no consumer names (a function key, a
 * multimedia code) arrives the same way and is ignored by whoever does not
 * want it. Pointer events pass through untouched.
 *
 * Keypad digits and the dot map to their ASCII values (numlock is assumed:
 * a terminal wants the numbers more than it wants the navigation codes).
 */
[[nodiscard]] visor::host::Event fold_text(visor::host::Event event) noexcept;

/// The table half, exposed for the judgments: the code point one evdev key
/// carries under one shift state, or the key's own number when the table has
/// no entry for it.
[[nodiscard]] std::uint32_t text_code_for(std::uint16_t evdev_code, bool shift) noexcept;

/// The wire's own number for a text code: the exact inverse walk of
/// text_code_for, main block before keypad, either shift spelling accepted.
/// Space spells 0x20 in the text world and 57 on the wire, so it folds
/// back first, before the runs are walked. Zero for every code the table
/// does not type -- the core's named keys (Backspace 14, Enter 28, the
/// out-of-band arrows) ride their evdev values already and pass through
/// untouched, and zero is no key's number on the wire.
[[nodiscard]] std::uint32_t evdev_code_for(std::uint32_t typable) noexcept;

/// The desk-side wrapper for a host that speaks raw evdev numbers: a table
/// whose key words cross the fold before the desk sees them. The wayland
/// binding is that host on purpose (the binding that knows a layout
/// produces text, and the wayland binding deliberately also does not), while
/// the consumers inside a desk -- terminal, icon, button -- eat code points.
/// Flush and clock pass straight through their own thunks, each carrying
/// its own context.
class TextFoldTable final {
  public:
    explicit TextFoldTable(visor::host::HostTable wrapped) noexcept;

    [[nodiscard]] visor::host::HostTable table() noexcept;

  private:
    static void flush_thunk(void* context, const visor::host::FlushRegion& region) noexcept;
    static bool poll_thunk(void* context, visor::host::Event& out) noexcept;
    static std::uint64_t clock_thunk(void* context) noexcept;

    visor::host::HostTable wrapped_;
};

} // namespace vesper::host
