#include "cases.hpp"
#include "testing.hpp"

#include "fonts/demo_font.hpp"
#include "host/fake_session_host.hpp"
#include "ui/terminal/terminal.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/events/key_words.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/run_loop.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace {

/// Writes down every byte the program receives, the judgment's witness for
/// the words the keyboard sends (the loopback cannot testify: it eats the
/// bytes into the screen).
class RecordingSource final : public vesper::desktop::TerminalSource {
  public:
    void send(std::string_view bytes) noexcept override { sent.append(bytes); }
    std::string sent;
};

using vesper::desktop::LoopbackSource;
using vesper::desktop::Terminal;
using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
using visor::host::KeyDown;

constexpr SizeI kScreen{.w = 480, .h = 320};
constexpr RectI kScreenBounds{.pos = PointI{0, 0}, .size = kScreen};
using Frame = visor::canvas::Canvas<kScreen>;

// The 80x25 screen the handover names: 480 is exactly 80 columns of the
// demo font's 6-advance cell, 175 exactly 25 rows of its 7-tall line.
constexpr RectI kFullTerm{.pos = PointI{0, 0}, .size = SizeI{.w = 480, .h = 175}};

// A 4x3 grid for the wrap and scroll judgments: 24x21 cut by the 6x7 cell.
constexpr RectI kSmallTerm{.pos = PointI{0, 0}, .size = SizeI{.w = 24, .h = 21}};

int lit_pixels(const vesper::host::FakeSessionHost& host) {
    int lit = 0;
    for (int y = 0; y < kScreen.h; ++y) {
        for (int x = 0; x < kScreen.w; ++x) {
            if (host.get(PointI{x, y})) {
                ++lit;
            }
        }
    }
    return lit;
}

} // namespace

void run_terminal_tests() {
    // Grid derivation: the bounds cut into whole cells, cursor home, every
    // cell born blank.
    {
        Terminal term{kFullTerm, vesper::fonts::kDemoFont};
        VESPER_CHECK_EQ(term.cols(), 80);
        VESPER_CHECK_EQ(term.rows(), 25);
        VESPER_CHECK_EQ(term.cursor_col(), 0);
        VESPER_CHECK_EQ(term.cursor_row(), 0);
        VESPER_CHECK_EQ(term.cell(0, 0), ' ');
        VESPER_CHECK_EQ(term.cell(79, 24), ' ');
    }

    // receive() semantics on the small grid: printable lands and steps.
    {
        Terminal term{kSmallTerm, vesper::fonts::kDemoFont};
        term.receive("AB");
        VESPER_CHECK_EQ(term.cell(0, 0), 'A');
        VESPER_CHECK_EQ(term.cell(1, 0), 'B');
        VESPER_CHECK_EQ(term.cursor_col(), 2);
        VESPER_CHECK_EQ(term.cursor_row(), 0);
    }

    // Wrap: the fifth column of a 4-column grid lands on the next row.
    {
        Terminal term{kSmallTerm, vesper::fonts::kDemoFont};
        term.receive("ABCDE");
        VESPER_CHECK_EQ(term.cell(3, 0), 'D');
        VESPER_CHECK_EQ(term.cell(0, 1), 'E');
        VESPER_CHECK_EQ(term.cursor_col(), 1);
        VESPER_CHECK_EQ(term.cursor_row(), 1);
    }

    // Newline goes to the next row's start; carriage return only rewinds.
    {
        Terminal term{kSmallTerm, vesper::fonts::kDemoFont};
        term.receive("AB\nC");
        VESPER_CHECK_EQ(term.cell(0, 1), 'C');
        VESPER_CHECK_EQ(term.cursor_col(), 1);
        VESPER_CHECK_EQ(term.cursor_row(), 1);
        term.receive("\rD");
        VESPER_CHECK_EQ(term.cell(0, 1), 'D');
        VESPER_CHECK_EQ(term.cell(1, 1), ' ');
    }

    // Backspace steps back and blanks; at a line start it stays put.
    {
        Terminal term{kSmallTerm, vesper::fonts::kDemoFont};
        term.receive("AB\b");
        VESPER_CHECK_EQ(term.cell(1, 0), ' ');
        VESPER_CHECK_EQ(term.cell(0, 0), 'A');
        VESPER_CHECK_EQ(term.cursor_col(), 1);
        term.receive("\b\b\b");
        VESPER_CHECK_EQ(term.cursor_col(), 0);
        VESPER_CHECK_EQ(term.cell(0, 0), ' ');
    }

    // Scroll: a fourth line on a 3-line grid pushes the first one off.
    {
        Terminal term{kSmallTerm, vesper::fonts::kDemoFont};
        term.receive("1\n2\n3\n4");
        VESPER_CHECK_EQ(term.cell(0, 0), '2');
        VESPER_CHECK_EQ(term.cell(0, 1), '3');
        VESPER_CHECK_EQ(term.cell(0, 2), '4');
        VESPER_CHECK_EQ(term.cursor_col(), 1);
        VESPER_CHECK_EQ(term.cursor_row(), 2);
    }

    // The echo chain: focus, router, code-to-byte, loopback, grid. This is
    // the D3 flagship judgment's logic half -- a keystroke the X11 host
    // would deliver as a code point paints itself through the whole seam.
    {
        Terminal term{kFullTerm, vesper::fonts::kDemoFont};
        LoopbackSource loop{term};
        term.set_source(&loop);
        term.set_focused(true);
        visor::ui::InputRouter input;
        visor::ui::dispatch_event(term, input, visor::host::Event{KeyDown{.code = 'h'}}, 0, false);
        visor::ui::dispatch_event(term, input, visor::host::Event{KeyDown{.code = 'i'}}, 0, false);
        VESPER_CHECK_EQ(term.cell(0, 0), 'h');
        VESPER_CHECK_EQ(term.cell(1, 0), 'i');
        VESPER_CHECK_EQ(term.cursor_col(), 2);
        VESPER_CHECK_EQ(term.cursor_row(), 0);

        // Enter by name, space by code point, backspace by name.
        visor::ui::dispatch_event(
            term, input, visor::host::Event{KeyDown{.code = visor::host::kKeyEnter}}, 0, false);
        VESPER_CHECK_EQ(term.cursor_col(), 0);
        VESPER_CHECK_EQ(term.cursor_row(), 1);
        visor::ui::dispatch_event(
            term, input, visor::host::Event{KeyDown{.code = visor::host::kCharSpace}}, 0, false);
        VESPER_CHECK_EQ(term.cursor_col(), 1);
        visor::ui::dispatch_event(
            term, input, visor::host::Event{KeyDown{.code = visor::host::kKeyBackspace}}, 0, false);
        VESPER_CHECK_EQ(term.cursor_col(), 0);

        // The evdev space number shares the wire with the code point; the
        // screen reads both.
        visor::ui::dispatch_event(
            term, input, visor::host::Event{KeyDown{.code = visor::host::kKeySpace}}, 0, false);
        VESPER_CHECK_EQ(term.cell(0, 1), ' ');
        VESPER_CHECK_EQ(term.cursor_col(), 1);
    }

    // An unwired screen drops keystrokes quietly, and nothing else changes.
    {
        Terminal term{kFullTerm, vesper::fonts::kDemoFont};
        term.on_key_down('h', visor::host::Modifiers{});
        VESPER_CHECK_EQ(term.cell(0, 0), ' ');
        VESPER_CHECK_EQ(term.cursor_col(), 0);
    }

    // The repaint debt: receive() owes the screen, and a keystroke that
    // round-trips through the loopback owes it again. Both debts merge into
    // one resident region (overlapping pushes merge), so the judgment is
    // payment: each round of run_dirty flushes, and an idle round does not.
    {
        Terminal term{kFullTerm, vesper::fonts::kDemoFont};
        LoopbackSource loop{term};
        term.set_source(&loop);
        visor::ui::DirtyRegions dirty;
        term.set_dirty_sink(&dirty);
        vesper::host::FakeSessionHost host{480, 320};
        visor::ui::InputRouter input;
        using Strip = visor::canvas::Canvas<SizeI{.w = 480, .h = 64}>;
        Strip strip;

        term.receive("x");
        static_cast<void>(visor::ui::run_dirty(host.table(), term, input, strip, kScreen, dirty));
        VESPER_CHECK(host.flush_count() > 0); // the debt paid, strip by strip
        VESPER_CHECK_EQ(dirty.count(), std::size_t{0});
        const int paid = host.flush_count();
        term.on_key_down('y', visor::host::Modifiers{});
        static_cast<void>(visor::ui::run_dirty(host.table(), term, input, strip, kScreen, dirty));
        VESPER_CHECK(host.flush_count() > paid); // the keystroke owed again
        const int paid_twice = host.flush_count();
        static_cast<void>(visor::ui::run_dirty(host.table(), term, input, strip, kScreen, dirty));
        VESPER_CHECK_EQ(host.flush_count(), paid_twice); // idle owes nothing
    }

    // The D3 flagship judgment: the 80x25 screen, off screen, one frame. The
    // screen paints as an inverted box -- before any typing the only unlit
    // pixels inside it are the caret's own 6x7 block, so the lit count is
    // box-minus-caret exactly. Typing punches glyph holes, so the count
    // drops, and the caret reads unlit at its new grid position.
    {
        vesper::host::FakeSessionHost host{480, 320};
        Terminal term{kFullTerm, vesper::fonts::kDemoFont};
        LoopbackSource loop{term};
        term.set_source(&loop);
        Frame frame;
        visor::ui::render_window(term, frame, kScreenBounds, host.table());
        VESPER_CHECK_EQ(lit_pixels(host), (480 * 175) - (6 * 7));

        term.receive("OK");
        vesper::host::FakeSessionHost typed_host{480, 320};
        visor::ui::render_window(term, frame, kScreenBounds, typed_host.table());
        VESPER_CHECK(lit_pixels(typed_host) < (480 * 175) - (6 * 7));
        // The caret moved to column 2: its block reads unlit, and the ground
        // at the box's far corner still reads lit.
        VESPER_CHECK(!typed_host.get(PointI{13, 3}));
        VESPER_CHECK(typed_host.get(PointI{479, 174}));
        // Glyph ink: some pixel in the first two columns' band is unlit
        // while the box around it stays lit.
        int unlit_band = 0;
        for (int y = 0; y < 7; ++y) {
            for (int x = 0; x < 12; ++x) {
                if (!typed_host.get(PointI{x, y})) {
                    ++unlit_band;
                }
            }
        }
        VESPER_CHECK(unlit_band > 0);
    }

    // ANSI, the trio a real prompt emits plus the swallow rules: erase in
    // line after a carriage return (the prompt redraw), cursor position,
    // erase in screen, tab stops, private sequences eaten whole.
    {
        Terminal term{kSmallTerm, vesper::fonts::kDemoFont};
        term.receive("ABCD"); // fills row 0, cursor wraps to (0, 1)
        term.receive("XY");
        term.receive("\r\x1b[K"); // the prompt redraw: rewind, erase rest
        VESPER_CHECK_EQ(term.cell(0, 0), 'A');
        VESPER_CHECK_EQ(term.cell(0, 1), ' ');
        VESPER_CHECK_EQ(term.cell(1, 1), ' ');
        VESPER_CHECK_EQ(term.cursor_col(), 0);
        VESPER_CHECK_EQ(term.cursor_row(), 1);
        term.receive("\x1b[2;3H"); // 1-based row;column -> cell (2, 1)
        VESPER_CHECK_EQ(term.cursor_col(), 2);
        VESPER_CHECK_EQ(term.cursor_row(), 1);
        term.receive("Z");
        VESPER_CHECK_EQ(term.cell(2, 1), 'Z');
        term.receive("\x1b[2J"); // everything blank, cursor stays
        VESPER_CHECK_EQ(term.cell(0, 0), ' ');
        VESPER_CHECK_EQ(term.cell(2, 1), ' ');
        VESPER_CHECK_EQ(term.cursor_col(), 3);
    }

    // vt100's four cursor arrows on the wire: A up, B down, C right, D left.
    // Zero and a missing parameter both mean one step, and the screen's
    // edges clamp the move. The redraw a line editor sends when it walks
    // history is this judgment's stage: carriage return homes the pen, CSI C
    // skips the prompt, CSI K clears the line from there, and the content
    // lands past the prompt -- the prompt itself survives, which a skipped
    // CSI C used to erase.
    {
        Terminal term{kSmallTerm, vesper::fonts::kDemoFont};
        term.receive("> l");    // the prompt and a one-letter word, cursor at 3
        term.receive("\x1b[D"); // one left
        VESPER_CHECK_EQ(term.cursor_col(), 2);
        term.receive("\x1b[2D"); // two left
        VESPER_CHECK_EQ(term.cursor_col(), 0);
        term.receive("\x1b[9D"); // clamped at the left edge
        VESPER_CHECK_EQ(term.cursor_col(), 0);
        term.receive("\x1b[C"); // one right
        VESPER_CHECK_EQ(term.cursor_col(), 1);
        term.receive("\x1b[9C"); // clamped inside the 4-wide screen
        VESPER_CHECK_EQ(term.cursor_col(), 3);
        term.receive("\x1b[B"); // one down
        VESPER_CHECK_EQ(term.cursor_row(), 1);
        term.receive("\x1b[9B"); // clamped at the bottom
        VESPER_CHECK_EQ(term.cursor_row(), 2);
        term.receive("\x1b[2A"); // two up
        VESPER_CHECK_EQ(term.cursor_row(), 0);

        // The history walk itself: home, skip the prompt, clear the rest,
        // write the new word.
        term.receive("\r");      // home
        term.receive("\x1b[2C"); // skip "> "
        VESPER_CHECK_EQ(term.cursor_col(), 2);
        term.receive("\x1b[K");                // clear the word from the pen on
        VESPER_CHECK_EQ(term.cell(0, 0), '>'); // the prompt survives
        VESPER_CHECK_EQ(term.cell(1, 0), ' ');
        VESPER_CHECK_EQ(term.cell(2, 0), ' '); // the old word is gone
        term.receive("ec");                    // the new word arrives
        VESPER_CHECK_EQ(term.cell(2, 0), 'e');
        VESPER_CHECK_EQ(term.cell(3, 0), 'c');
    }

    // A sequence split across two receive() calls still lands: the escape
    // walker keeps its seat between batches.
    {
        Terminal term{kSmallTerm, vesper::fonts::kDemoFont};
        term.receive("AB\x1b[");
        VESPER_CHECK_EQ(term.cell(0, 0), 'A');
        term.receive("K"); // the tail arrives, the erase happens
        VESPER_CHECK_EQ(term.cell(1, 0), 'B');
        VESPER_CHECK_EQ(term.cell(2, 0), ' ');
    }

    // Private sequences (bracketed paste, cursor modes) and unknown finals
    // are swallowed whole: parsed, not obeyed, never painted.
    {
        Terminal term{kSmallTerm, vesper::fonts::kDemoFont};
        term.receive("\x1b[?2004hQ");
        VESPER_CHECK_EQ(term.cell(0, 0), 'Q');
        term.receive("\x1b[1mR"); // SGR is not this screen's business
        VESPER_CHECK_EQ(term.cell(1, 0), 'R');
    }

    // Tab stops every eight columns.
    {
        Terminal term{kFullTerm, vesper::fonts::kDemoFont};
        term.receive("A\tB");
        VESPER_CHECK_EQ(term.cell(0, 0), 'A');
        VESPER_CHECK_EQ(term.cell(7, 0), ' ');
        VESPER_CHECK_EQ(term.cell(8, 0), 'B');
        VESPER_CHECK_EQ(term.cursor_col(), 9);
    }
}
