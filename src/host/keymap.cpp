#include "host/keymap.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>

#include "visor/bindings/events.hpp"

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
        return event;
    }
    if (auto* up = std::get_if<visor::host::KeyUp>(&event)) {
        up->code = text_code_for(static_cast<std::uint16_t>(up->code), up->modifiers.shift);
    }
    return event;
}

} // namespace vesper::host
