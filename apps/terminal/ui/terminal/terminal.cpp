#include "ui/terminal/terminal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/events/key_words.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/text.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace vesper::desktop {
namespace {

// The xterm sixteen, verbatim values: the classic SGR palette every
// colored program on this machine already speaks. Index 0..7 the dim
// half, 8..15 the bright half.
constexpr std::array<visor::canvas::Ink, 16> kSgrPalette{
    visor::canvas::Ink::from_rgb(0x000000), visor::canvas::Ink::from_rgb(0xcd0000),
    visor::canvas::Ink::from_rgb(0x00cd00), visor::canvas::Ink::from_rgb(0xcdcd00),
    visor::canvas::Ink::from_rgb(0x0000ee), visor::canvas::Ink::from_rgb(0xcd00cd),
    visor::canvas::Ink::from_rgb(0x00cdcd), visor::canvas::Ink::from_rgb(0xe5e5e5),
    visor::canvas::Ink::from_rgb(0x7f7f7f), visor::canvas::Ink::from_rgb(0xff0000),
    visor::canvas::Ink::from_rgb(0x00ff00), visor::canvas::Ink::from_rgb(0xffff00),
    visor::canvas::Ink::from_rgb(0x5c5cff), visor::canvas::Ink::from_rgb(0xff00ff),
    visor::canvas::Ink::from_rgb(0x00ffff), visor::canvas::Ink::from_rgb(0xffffff),
};

} // namespace

using visor::base::PointI;
using visor::base::RectI;

namespace {

/// One key code to the single byte a byte source expects; false when the
/// code names nothing byte-shaped. The named keys carry key-block numbers
/// rather than code points, so each is spelled out, and kKeySpace needs the
/// case before the band sees it: by the events convention 57 means the
/// space bar to this consumer, not the digit '9' it doubles as on the wire.
[[nodiscard]] bool byte_for_key(std::uint32_t code, char& out) noexcept {
    switch (code) {
        case visor::host::kKeyBackspace:
            out = '\b';
            return true;
        case visor::host::kKeyEnter:
            out = '\n';
            return true;
        case visor::host::kKeyTab:
            out = '\t';
            return true;
        case visor::host::kCharSpace:
        case visor::host::kKeySpace:
            out = ' ';
            return true;
        default:
            if (code > visor::host::kCharSpace && code <= 0x7e) {
                out = static_cast<char>(code);
                return true;
            }
            return false;
    }
}

} // namespace

Terminal::Terminal(RectI bounds, const visor::draw::MonoFont& font) noexcept
    : Widget{bounds}, font_{font} {
    // The cell is the font's own measure: the widest advance any glyph
    // carries (a strip-cut font bakes its gap into one uniform advance) and
    // the line height. A font with no glyphs at all still gets a 1x1 cell
    // rather than a zero division.
    for (const visor::draw::GlyphEntry& glyph : font_.glyphs) {
        cell_w_ = std::max(cell_w_, static_cast<int>(glyph.advance));
    }
    cell_h_ = std::max(1, static_cast<int>(font_.line_height));
    cols_ = std::max(bounds.size.w / cell_w_, 1);
    rows_ = std::max(bounds.size.h / cell_h_, 1);
    cells_.assign(cell_index(cols_ - 1, rows_ - 1) + 1, ' ');
    attrs_.assign(cells_.size(), CellAttr{});
}

void Terminal::set_source(TerminalSource* source) noexcept {
    source_ = source;
}

bool Terminal::capture_event(const visor::host::Event& event) noexcept {
    if (!focused()) {
        return false;
    }
    if (const auto* down = std::get_if<visor::host::KeyDown>(&event);
        down != nullptr && down->code == visor::host::kKeyTab) {
        on_key_down(down->code, down->modifiers);
        return true;
    }
    const auto* up = std::get_if<visor::host::KeyUp>(&event);
    return up != nullptr && up->code == visor::host::kKeyTab;
}

std::size_t Terminal::cell_index(int col, int row) const noexcept {
    return (static_cast<std::size_t>(row) * static_cast<std::size_t>(cols_)) +
           static_cast<std::size_t>(col);
}

char Terminal::cell(int col, int row) const noexcept {
    return cells_[cell_index(col, row)];
}

void Terminal::receive(std::string_view bytes) noexcept {
    for (const char c : bytes) {
        const auto byte = static_cast<unsigned char>(c);

        if (esc_ == EscState::Esc) {
            // One byte after ESC decides: '[' opens a CSI sequence; anything
            // else is a two-byte escape whose meaning this screen does not
            // need, swallowed whole.
            if (byte == '[') {
                esc_ = EscState::Bracket;
                csi_params_.fill(0);
                csi_param_count_ = 0;
                csi_private_ = false;
            } else {
                esc_ = EscState::Ground;
            }
            continue;
        }
        if (esc_ == EscState::Bracket) {
            if (std::isdigit(byte) != 0) {
                csi_params_[static_cast<std::size_t>(csi_param_count_)] =
                    (csi_params_[static_cast<std::size_t>(csi_param_count_)] * 10) + (byte - '0');
            } else if (byte == ';') {
                if (csi_param_count_ + 1 < static_cast<int>(csi_params_.size())) {
                    ++csi_param_count_;
                }
            } else if (byte >= 0x3c && byte <= 0x3f) {
                // '?' and neighbors mark a private sequence (mode set and
                // reset); none of the actions are honored for those.
                csi_private_ = true;
            } else if (byte >= 0x20 && byte <= 0x2f) {
                // Intermediate bytes carry refinements this screen ignores.
            } else if (byte >= 0x40 && byte <= 0x7e) {
                apply_csi(byte);
                esc_ = EscState::Ground;
            } else {
                // A control byte inside the sequence aborts it; the byte
                // goes with it.
                esc_ = EscState::Ground;
            }
            continue;
        }

        if (byte == 0x1b) {
            esc_ = EscState::Esc;
        } else if (byte == '\b') {
            // Backspace: the pen steps back one cell and blanks it. A cursor
            // already at a line start stays put -- wrapping the erase upward
            // is a real terminal's trick, not this screen's.
            if (cursor_col_ > 0) {
                --cursor_col_;
            }
            cells_[cell_index(cursor_col_, cursor_row_)] = ' ';
            attrs_[cell_index(cursor_col_, cursor_row_)] = CellAttr{};
        } else if (byte == '\n') {
            // One byte, the plain-echo pair: next row, pen at the line start.
            newline();
            cursor_col_ = 0;
        } else if (byte == '\r') {
            cursor_col_ = 0;
        } else if (byte == '\t') {
            // Tab stops every eight columns, the 80-column habit.
            const int next_stop = ((cursor_col_ / 8) + 1) * 8;
            cursor_col_ = std::min(next_stop, cols_ - 1);
        } else if (byte >= 0x20 && byte <= 0x7e) {
            put(byte);
        }
        // The remaining control bytes (bell and company) are not this
        // screen's business.
    }
    // One batch, one repaint debt. The text can land anywhere inside the
    // screen, and the screen is small enough that the whole box is the
    // honest report.
    invalidate(bounds());
}

void Terminal::erase_cells(int col_from, int col_to, int row) noexcept {
    const int first = std::max(col_from, 0);
    const int last = std::min(col_to, cols_ - 1);
    for (int col = first; col <= last; ++col) {
        // Erase takes the dress with the glyph: a cleared screen owes no
        // stray palette corners, and repaint-from-scratch apps (the CSI 2J
        // crowd) start from the screen's own default again.
        cells_[cell_index(col, row)] = ' ';
        attrs_[cell_index(col, row)] = CellAttr{};
    }
}

void Terminal::style_for_sgr(int word) noexcept {
    if (word == 0) {
        // Reset: the screen's own inverted default, bold gone with the rest.
        style_ = CellAttr{};
        style_bold_ = false;
    } else if (word == 1) {
        style_bold_ = true;
    } else if (word == 22) {
        style_bold_ = false;
    } else if (word >= 30 && word <= 37) {
        // Dim foreground, brightened by the bold bit: the classic linkage
        // a colored prompt on this desk expects.
        style_.fg = static_cast<std::uint8_t>(1 + (word - 30) + (style_bold_ ? 8 : 0));
    } else if (word == 39) {
        style_.fg = 0;
    } else if (word >= 40 && word <= 47) {
        style_.bg = static_cast<std::uint8_t>(1 + (word - 40));
    } else if (word == 49) {
        style_.bg = 0;
    } else if (word >= 90 && word <= 97) {
        style_.fg = static_cast<std::uint8_t>(1 + 8 + (word - 90));
    } else if (word >= 100 && word <= 107) {
        style_.bg = static_cast<std::uint8_t>(1 + 8 + (word - 100));
    }
    // Every other word (underline, italic, inverse and company) is parsed
    // and dropped: this screen has no dress for them yet.
}

void Terminal::apply_csi(unsigned char final_byte) noexcept {
    if (csi_private_) {
        return;
    }
    if (final_byte == 'm') {
        for (int i = 0; i <= csi_param_count_; ++i) {
            style_for_sgr(csi_params_[static_cast<std::size_t>(i)]);
        }
        return;
    }
    const int first = csi_params_[0];
    const int second = csi_params_[1];
    if (final_byte == 'K') {
        // Erase in line: 0 cursor-to-end (the default), 1 start-to-cursor,
        // 2 the whole line.
        if (first == 0) {
            erase_cells(cursor_col_, cols_ - 1, cursor_row_);
        } else if (first == 1) {
            erase_cells(0, cursor_col_, cursor_row_);
        } else if (first == 2) {
            erase_cells(0, cols_ - 1, cursor_row_);
        }
    } else if (final_byte == 'J') {
        // Erase in screen: 0 cursor-to-end (the default), 1 start-to-
        // cursor, 2 everything. The cursor stays put on all three.
        if (first == 0) {
            erase_cells(cursor_col_, cols_ - 1, cursor_row_);
            for (int row = cursor_row_ + 1; row < rows_; ++row) {
                erase_cells(0, cols_ - 1, row);
            }
        } else if (first == 1) {
            for (int row = 0; row < cursor_row_; ++row) {
                erase_cells(0, cols_ - 1, row);
            }
            erase_cells(0, cursor_col_, cursor_row_);
        } else if (first == 2) {
            for (int row = 0; row < rows_; ++row) {
                erase_cells(0, cols_ - 1, row);
            }
        }
    } else if (final_byte == 'H' || final_byte == 'f') {
        // Cursor position, 1-based row;column; defaults and zeros both home.
        cursor_row_ = std::clamp(std::max(first, 1) - 1, 0, rows_ - 1);
        cursor_col_ = std::clamp(std::max(second, 1) - 1, 0, cols_ - 1);
    } else if (final_byte == 'A' || final_byte == 'B' || final_byte == 'C' || final_byte == 'D') {
        // vt100's own four: A up, B down, C right, D left. Zero and a missing
        // parameter both mean one step, and the screen's edges clamp the
        // move -- a plain arrow never wraps. These are the words a line
        // editor's redraw speaks: carriage return homes the pen, CSI C skips
        // the prompt, CSI K clears from there, and the prompt survives
        // because the skip landed the pen past it.
        const int steps = std::max(first, 1);
        switch (final_byte) {
            case 'A':
                cursor_row_ = std::max(cursor_row_ - steps, 0);
                break;
            case 'B':
                cursor_row_ = std::min(cursor_row_ + steps, rows_ - 1);
                break;
            case 'C':
                cursor_col_ = std::min(cursor_col_ + steps, cols_ - 1);
                break;
            case 'D':
                cursor_col_ = std::max(cursor_col_ - steps, 0);
                break;
            default:
                break;
        }
    }
    // Every other final byte is swallowed: parsed, not obeyed.
}

void Terminal::on_key_down(std::uint32_t code, visor::host::Modifiers modifiers) noexcept {
    // Printable codes already carry the host's shift folding (the text-host
    // convention); the ctrl bit is folded here, the one modifier that
    // changes the byte a terminal sends.
    if (source_ == nullptr) {
        return;
    }
    // The arrows are the one named pair this screen forwards instead of
    // eating: a readline behind a pty asks for history as vt100 CSI words,
    // and a desk claiming TERM=vt100 owes them. The screen's own receive()
    // has no cursor-motion CSI, so a loopback of these words paints nothing
    // here -- the pty is where they matter.
    switch (code) {
        case visor::host::kKeyUp:
            source_->send("\x1b[A");
            return;
        case visor::host::kKeyDown:
            source_->send("\x1b[B");
            return;
        case visor::host::kKeyRight:
            source_->send("\x1b[C");
            return;
        case visor::host::kKeyLeft:
            source_->send("\x1b[D");
            return;
        default:
            break;
    }
    char byte = 0;
    if (!byte_for_key(code, byte)) {
        return;
    }
    // Ctrl + letter is the keyboard's oldest fold: the control byte the
    // letter's low five bits name -- Ctrl+C is INTR (0x03), the interrupt a
    // shell answers to, Ctrl+D is EOF (0x04). A ctrl held over anything but
    // a letter adds nothing this screen sends.
    if (modifiers.ctrl && byte >= 'a' && byte <= 'z') {
        byte = static_cast<char>(static_cast<unsigned char>(byte) & 0x1fU);
    }
    source_->send(std::string_view{&byte, 1});
}

void Terminal::put(unsigned char byte) noexcept {
    cells_[cell_index(cursor_col_, cursor_row_)] = static_cast<char>(byte);
    attrs_[cell_index(cursor_col_, cursor_row_)] = style_;
    ++cursor_col_;
    if (cursor_col_ == cols_) {
        cursor_col_ = 0;
        newline();
    }
}

void Terminal::newline() noexcept {
    if (cursor_row_ + 1 == rows_) {
        // The bottom line is the frontier: the grid gives up its top line
        // and the newcomer scrolls in at the bottom.
        const auto line = static_cast<std::size_t>(cols_);
        std::copy(cells_.begin() + static_cast<std::ptrdiff_t>(line), cells_.end(), cells_.begin());
        std::fill(cells_.end() - static_cast<std::ptrdiff_t>(line), cells_.end(), ' ');
        std::copy(attrs_.begin() + static_cast<std::ptrdiff_t>(line), attrs_.end(), attrs_.begin());
        std::fill(attrs_.end() - static_cast<std::ptrdiff_t>(line), attrs_.end(), CellAttr{});
        return;
    }
    ++cursor_row_;
}

void Terminal::on_draw(const visor::ui::Painter& painter) const noexcept {
    // An inverted screen by default: lit ground, glyphs and cursor as
    // unlit holes -- the dress SGR 0 restores to. A cell that carries SGR
    // words paints over that default with the palette's own inks: its
    // ground fills the cell, its glyph rides the raw-ink text lane.
    painter.fill(bounds(), visor::canvas::ColorRole::Foreground);
    for (int row = 0; row < rows_; ++row) {
        for (int col = 0; col < cols_; ++col) {
            const std::size_t index = cell_index(col, row);
            const CellAttr attr = attrs_[index];
            // One glyph per cell, at the cell's own origin: a character the
            // font has no glyph for paints nothing, and every later cell
            // stays on its grid position regardless.
            const PointI pos{bounds().pos.x + (col * cell_w_), bounds().pos.y + (row * cell_h_)};
            if (attr.bg != 0) {
                const RectI cell_rect{
                    .pos = pos,
                    .size = visor::base::SizeI{.w = cell_w_, .h = cell_h_},
                };
                painter.fill(cell_rect, kSgrPalette[attr.bg - 1]);
            }
            if (cells_[index] == ' ') {
                continue;
            }
            if (attr.fg != 0) {
                visor::ui::draw_text_clipped(painter, bounds(), pos, font_,
                                             std::string_view{&cells_[index], 1},
                                             kSgrPalette[attr.fg - 1], 1);
            } else {
                visor::ui::draw_text_clipped(painter, bounds(), pos, font_,
                                             std::string_view{&cells_[index], 1},
                                             visor::canvas::ColorRole::Background, 1);
            }
        }
    }
    // The cursor is one solid cell block in the scheme's accent -- on the
    // mono screen it folds back to the unlit white it always was, and on a
    // color desk it is the desk's gold, so the caret reads before any text
    // does.
    const RectI caret{
        .pos = PointI{bounds().pos.x + (cursor_col_ * cell_w_),
                      bounds().pos.y + (cursor_row_ * cell_h_)},
        .size = visor::base::SizeI{.w = cell_w_, .h = cell_h_},
    };
    painter.fill(caret, visor::canvas::ColorRole::Accent);
}

} // namespace vesper::desktop
