#include "cases.hpp"
#include "testing.hpp"

#include "fonts/term_font.hpp"
#include "fonts/title_font.hpp"
#include "fonts/ui_font.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/canvas_painter.hpp"
#include "visor/ui/text.hpp"
#include "visor/ui/theme.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace {

using visor::base::PointI;
using visor::base::SizeI;
using visor::canvas::ColorRole;

constexpr SizeI kSheetSize{.w = 64, .h = 16};
using Sheet = visor::canvas::Canvas<kSheetSize>;

/// The contract every face in src/fonts has to keep: the whole printable
/// ASCII block is present, every glyph moves the pen, and no ink reaches
/// above the line or past its bottom. A face that misses a glyph paints
/// nothing for that byte and measures nothing, which reads as a mystery gap
/// in the middle of a word.
void check_face(const visor::draw::MonoFont& font, std::size_t line_height) {
    VESPER_CHECK_EQ(font.line_height, line_height);
    for (std::uint32_t codepoint = 0x20; codepoint <= 0x7e; ++codepoint) {
        const visor::draw::GlyphEntry* glyph = font.glyph(codepoint);
        VESPER_CHECK(glyph != nullptr);
        if (glyph == nullptr) {
            return;
        }
        VESPER_CHECK(glyph->advance > 0);
        VESPER_CHECK(glyph->offset_y >= 0);
        VESPER_CHECK((static_cast<int>(glyph->offset_y) + static_cast<int>(glyph->height)) <=
                     static_cast<int>(line_height));
    }
}

/// Paints one byte and reports how many pixels it lit and whether all of
/// them stayed inside the box the layout cursor predicted.
void paint_glyph(const visor::draw::MonoFont& font, char byte, int& lit, bool& inside) {
    Sheet sheet;
    const visor::ui::CanvasPainter painter{sheet, visor::ui::kLightTheme};
    const SizeI measured = visor::draw::measure_text(font, std::string_view{&byte, 1}, 1);
    const int line_bottom = static_cast<int>(font.line_height);
    visor::ui::draw_text(painter, PointI{0, 0}, font, std::string_view{&byte, 1},
                         ColorRole::Foreground, 1);
    lit = 0;
    inside = true;
    for (int y = 0; y < kSheetSize.h; ++y) {
        for (int x = 0; x < kSheetSize.w; ++x) {
            if (!sheet.get(PointI{x, y})) {
                continue;
            }
            ++lit;
            if (x >= measured.w || y >= line_bottom) {
                inside = false;
            }
        }
    }
}

} // namespace

void run_font_tests() {
    // The three shipped faces: the small UI face, the bold title face and
    // the terminal's monospaced face.
    check_face(vesper::fonts::ui::kUiFont, vesper::fonts::ui::kLineHeight);
    check_face(vesper::fonts::title::kTitleFont, vesper::fonts::title::kLineHeight);
    check_face(vesper::fonts::term_font::kTermFont, vesper::fonts::term_font::kLineHeight);

    const visor::draw::MonoFont& ui = vesper::fonts::ui::kUiFont;
    const visor::draw::MonoFont& term = vesper::fonts::term_font::kTermFont;

    // Proportional where it should be: the UI face's pen moves by each
    // glyph's own advance, so four wide letters are wider than four narrow
    // ones. A strip-cut face would fail this by construction.
    VESPER_CHECK(visor::draw::measure_text(ui, "MMMM", 1).w >
                 visor::draw::measure_text(ui, "iiii", 1).w);

    // The terminal's grid is only honest if every cell is the same width:
    // the screen multiplies one cell width by the column index.
    std::size_t term_advance = 0;
    for (std::uint32_t codepoint = 0x20; codepoint <= 0x7e; ++codepoint) {
        const visor::draw::GlyphEntry* glyph = term.glyph(codepoint);
        VESPER_CHECK(glyph != nullptr);
        if (glyph == nullptr) {
            return;
        }
        if (term_advance == 0) {
            term_advance = glyph->advance;
        }
        VESPER_CHECK_EQ(glyph->advance, term_advance);
    }

    // Measurement is the layout cursor's own arithmetic: a two-letter word
    // is exactly the two advances, and the height is one line.
    const visor::draw::GlyphEntry* a = ui.glyph('A');
    const visor::draw::GlyphEntry* b = ui.glyph('B');
    const bool has_a = a != nullptr;
    const bool has_b = b != nullptr;
    VESPER_CHECK(has_a);
    VESPER_CHECK(has_b);
    if (has_a && has_b) {
        const SizeI measured = visor::draw::measure_text(ui, "AB", 1);
        VESPER_CHECK_EQ(measured.w, static_cast<int>(a->advance + b->advance));
        VESPER_CHECK_EQ(measured.h, static_cast<int>(ui.line_height));
    }

    // Every face actually paints its capital A, and the ink stays inside the
    // measured box: a glyph that overhangs the layout cursor would step on
    // the next character.
    const std::array<const visor::draw::MonoFont*, 3> faces{
        &ui,
        &vesper::fonts::title::kTitleFont,
        &term,
    };
    for (const visor::draw::MonoFont* face : faces) {
        int lit = 0;
        bool inside = false;
        paint_glyph(*face, 'A', lit, inside);
        VESPER_CHECK(lit > 0);
        VESPER_CHECK(inside);
    }
}
