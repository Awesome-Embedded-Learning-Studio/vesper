#include "host/keymap.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>

#include "visor/bindings/display.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/events/key_words.hpp"
#include "visor/bindings/host.hpp"

namespace vesper::host {
namespace {

/// One contiguous run of the evdev key block and the characters it types,
/// unshifted and shifted. The runs cover every key that produces text on a
/// US board; keys outside them (the core's named keys among those) keep
/// their own numbers. The keypad runs type their digits under numlock,
/// which a terminal wants far more than the navigation codes.
struct TextRun {
    std::uint16_t first_code;
    std::string_view plain;
    std::string_view shifted;
};

constexpr std::array<TextRun, 11> kRuns{
    {
        {.first_code = 2, .plain = "1234567890", .shifted = "!@#$%^&*()"},
        {.first_code = 12, .plain = "-=", .shifted = "_+"},
        {.first_code = 16, .plain = "qwertyuiop", .shifted = "QWERTYUIOP"},
        {.first_code = 26, .plain = "[]", .shifted = "{}"},
        {.first_code = 30, .plain = "asdfghjkl", .shifted = "ASDFGHJKL"},
        {.first_code = 39, .plain = ";'", .shifted = ":\""},
        {.first_code = 41, .plain = "`", .shifted = "~"},
        {.first_code = 43, .plain = "\\", .shifted = "|"},
        {.first_code = 44, .plain = "zxcvbnm", .shifted = "ZXCVBNM"},
        {.first_code = 51, .plain = ",./", .shifted = "<>?"},
        {.first_code = 71, .plain = "789-", .shifted = "789-"},
    },
};

/// The keypad's lower two rows sit at 75 and 79, past the run above; a
/// second scan keeps the main table's strings honest to the keyboard.
constexpr std::array<TextRun, 2> kKeypadRuns{
    {
        {.first_code = 75, .plain = "456+", .shifted = "456+"},
        {.first_code = 79, .plain = "1230.", .shifted = "1230."},
    },
};

/// Space arrives as the evdev 57 the runs pass through, but 57 doubles as
/// the digit '9' in the text world -- the same collision the X11 and fbdev
/// hosts already settle by spelling space 0x20. Fold it at the same gate.
void fold_space(std::uint32_t& code) noexcept {
    if (code == visor::host::kKeySpace) {
        code = visor::host::kCharSpace;
    }
}

} // namespace

std::uint32_t text_code_for(std::uint16_t code, bool shift) noexcept {
    char found = '\0';
    const auto scan = [&](const auto& table) {
        for (const TextRun& run : table) {
            if (code >= run.first_code && code < run.first_code + run.plain.size()) {
                const auto offset = static_cast<std::size_t>(code - run.first_code);
                const std::string_view text = shift ? run.shifted : run.plain;
                if (offset < text.size()) {
                    found = text[offset];
                }
                return;
            }
        }
    };
    scan(kRuns);
    if (found == '\0') {
        scan(kKeypadRuns);
    }
    return found != '\0' ? static_cast<std::uint32_t>(static_cast<unsigned char>(found))
                         : static_cast<std::uint32_t>(code);
}

visor::host::Event fold_text(visor::host::Event event) noexcept {
    if (auto* down = std::get_if<visor::host::KeyDown>(&event)) {
        down->code = text_code_for(static_cast<std::uint16_t>(down->code), down->modifiers.shift);
        fold_space(down->code);
        return event;
    }
    if (auto* up = std::get_if<visor::host::KeyUp>(&event)) {
        up->code = text_code_for(static_cast<std::uint16_t>(up->code), up->modifiers.shift);
        fold_space(up->code);
    }
    return event;
}

std::uint32_t evdev_code_for(const std::uint32_t kTypable) noexcept {
    // The exact inverse walk of text_code_for: both spellings of every run
    // accept the code, main block before keypad, first hit wins. The main
    // block first matters for the digits: '7' lives on both the number row
    // (8) and the keypad (73), and a desk that folds '7' back wants the
    // number row's key -- the wire's xkb table types the same character
    // from either, so the guest cannot tell the difference.
    if (kTypable == visor::host::kCharSpace) {
        return visor::host::kKeySpace; // the one key the runs do not carry
    }
    for (const TextRun& run : kRuns) {
        for (std::size_t i = 0; i < run.plain.size(); ++i) {
            const auto plain = static_cast<std::uint32_t>(static_cast<unsigned char>(run.plain[i]));
            const auto shifted =
                static_cast<std::uint32_t>(static_cast<unsigned char>(run.shifted[i]));
            if (kTypable == plain || kTypable == shifted) {
                return run.first_code + i;
            }
        }
    }
    for (const TextRun& run : kKeypadRuns) {
        for (std::size_t i = 0; i < run.plain.size(); ++i) {
            const auto plain = static_cast<std::uint32_t>(static_cast<unsigned char>(run.plain[i]));
            const auto shifted =
                static_cast<std::uint32_t>(static_cast<unsigned char>(run.shifted[i]));
            if (kTypable == plain || kTypable == shifted) {
                return run.first_code + i;
            }
        }
    }
    return 0;
}

TextFoldTable::TextFoldTable(visor::host::HostTable wrapped) noexcept : wrapped_{wrapped} {}

visor::host::HostTable TextFoldTable::table() noexcept {
    return visor::host::HostTable{
        .flush = &flush_thunk,
        .poll_event = &poll_thunk,
        .clock_ms = &clock_thunk,
        .context = this,
        .info = wrapped_.info,
    };
}

void TextFoldTable::flush_thunk(void* context, const visor::host::FlushRegion& region) noexcept {
    const TextFoldTable& self = *static_cast<const TextFoldTable*>(context);
    self.wrapped_.flush(self.wrapped_.context, region);
}

bool TextFoldTable::poll_thunk(void* context, visor::host::Event& out) noexcept {
    const TextFoldTable& self = *static_cast<const TextFoldTable*>(context);
    if (!self.wrapped_.poll(out)) {
        return false;
    }
    out = fold_text(out);
    return true;
}

std::uint64_t TextFoldTable::clock_thunk(void* context) noexcept {
    const TextFoldTable& self = *static_cast<const TextFoldTable*>(context);
    return self.wrapped_.clock_ms(self.wrapped_.context);
}

} // namespace vesper::host
