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
        if (byte == '\b') {
            // Backspace: the pen steps back one cell and blanks it. A cursor
            // already at a line start stays put -- wrapping the erase upward
            // is a real terminal's trick, not D3's.
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
        } else if (byte >= 0x20 && byte <= 0x7e) {
            put(byte);
        }
        // Every other byte is ANSI's business and waits for D4.
    }
    // One batch, one repaint debt. The text can land anywhere inside the
    // screen, and the screen is small enough that the whole box is the
    // honest report.
    invalidate(bounds());
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
