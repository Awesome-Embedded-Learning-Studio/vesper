#include "desktop/terminal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/text.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace vesper::desktop {

using visor::base::PointI;
using visor::base::RectI;

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
}

void Terminal::set_source(TerminalSource* source) noexcept {
    source_ = source;
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
                csi_params_ = {0, 0};
                csi_param_count_ = 0;
                csi_private_ = false;
            } else {
                esc_ = EscState::Ground;
            }
            continue;
        }
        if (esc_ == EscState::Bracket) {
            if (byte >= '0' && byte <= '9') {
                csi_params_[static_cast<std::size_t>(csi_param_count_)] =
                    (csi_params_[static_cast<std::size_t>(csi_param_count_)] * 10) + (byte - '0');
            } else if (byte == ';') {
                if (csi_param_count_ + 1 < 2) {
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
        cells_[cell_index(col, row)] = ' ';
    }
}

void Terminal::apply_csi(unsigned char final_byte) noexcept {
    if (csi_private_) {
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
    }
    // Every other final byte is swallowed: parsed, not obeyed.
}

void Terminal::on_key_down(std::uint32_t code, visor::host::Modifiers modifiers) noexcept {
    static_cast<void>(modifiers);
    // Printable codes already carry the host's shift folding (the text-host
    // convention), so the modifiers add nothing to a byte the code point
    // already names.
    char byte = 0;
    if (code == visor::host::kKeyBackspace) {
        byte = '\b';
    } else if (code == visor::host::kKeyEnter) {
        byte = '\n';
    } else if (code == visor::host::kCharSpace || code == visor::host::kKeySpace) {
        byte = ' ';
    } else if (code > visor::host::kCharSpace && code <= 0x7e) {
        byte = static_cast<char>(code);
    } else {
        return;
    }
    if (source_ != nullptr) {
        source_->send(std::string_view{&byte, 1});
    }
}

void Terminal::put(unsigned char byte) noexcept {
    cells_[cell_index(cursor_col_, cursor_row_)] = static_cast<char>(byte);
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
        return;
    }
    ++cursor_row_;
}

void Terminal::on_draw(const visor::ui::Painter& painter) const noexcept {
    // An inverted screen: lit ground, glyphs and cursor as unlit holes. One
    // ink, no second color, and the terminal still stands off the desk's
    // light ground.
    painter.fill(bounds(), visor::canvas::ColorRole::Foreground);
    for (int row = 0; row < rows_; ++row) {
        for (int col = 0; col < cols_; ++col) {
            const std::size_t index = cell_index(col, row);
            if (cells_[index] == ' ') {
                continue;
            }
            // One glyph per cell, at the cell's own origin: a character the
            // font has no glyph for paints nothing, and every later cell
            // stays on its grid position regardless.
            const PointI pos{bounds().pos.x + (col * cell_w_), bounds().pos.y + (row * cell_h_)};
            visor::ui::draw_text_clipped(painter, bounds(), pos, font_,
                                         std::string_view{&cells_[index], 1},
                                         visor::canvas::ColorRole::Background, 1);
        }
    }
    // The cursor is one unlit cell block, the classic solid caret.
    const RectI caret{
        .pos = PointI{bounds().pos.x + (cursor_col_ * cell_w_),
                      bounds().pos.y + (cursor_row_ * cell_h_)},
        .size = visor::base::SizeI{.w = cell_w_, .h = cell_h_},
    };
    painter.fill(caret, visor::canvas::ColorRole::Background);
}

} // namespace vesper::desktop
