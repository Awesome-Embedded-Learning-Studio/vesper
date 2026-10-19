#pragma once

#include <string_view>
#include <vector>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/bindings/events.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace vesper::desktop {

/**
 * @brief Where the terminal's keystrokes go: the program behind the screen.
 *
 * The terminal never talks to a device itself. It hands each keystroke to a
 * source as bytes and paints whatever bytes the source hands back through
 * receive(). D4's PTY is this same door with a real program on the far side,
 * which is the whole reason the fake source exists: the grid side is built
 * and judged against this seam, not against a device.
 */
class TerminalSource {
  public:
    virtual ~TerminalSource() = default;

    /// Keystrokes as bytes. The call consumes its argument before returning
    /// (the loopback answers inside it), so a short-lived buffer is safe.
    virtual void send(std::string_view bytes) noexcept = 0;
};

/**
 * @brief One terminal screen: a character grid, a cursor and an echo.
 *
 * The grid is cells of one glyph each, sized from the font at construction
 * (cell width is the font's widest advance, cell height its line height) and
 * cut from the bounds in whole cells. A bounds change after construction
 * does not re-grid: D3 has no reason to resize a terminal, so the honest
 * answer is to build it at its final size.
 *
 * Two doors, two directions. on_key_down turns key codes into bytes and
 * hands them to the source; receive() takes the source's bytes and lands
 * them in the grid. What receive() understands: printable ASCII goes into
 * the cell under the cursor, '\n' steps to the next row's start, '\r'
 * rewinds the pen to the line start, '\b' steps back one cell and blanks
 * it. Everything else is ANSI's business and waits for D4. Typing past the
 * last column wraps, and a newline on the last row scrolls the grid up one
 * line.
 *
 * The screen paints as an inverted box -- lit ground, glyphs and cursor
 * punched as unlit holes -- so one ink sets the terminal off the desk's
 * light ground without waiting for the color core.
 */
class Terminal final : public visor::ui::Widget {
  public:
    /// bounds is the whole screen. A source is wired after construction, the
    /// same pattern Button's activation takes; an unwired screen drops
    /// keystrokes quietly, the stance an unwired dirty sink takes.
    explicit Terminal(visor::base::RectI bounds, const visor::draw::MonoFont& font) noexcept;

    /// Wires the program behind the screen. Keystrokes before this call go
    /// nowhere.
    void set_source(TerminalSource* source) noexcept;

    /// The source's bytes land here: echo in D3, program output in D4.
    void receive(std::string_view bytes) noexcept;

    // Grid readback for judgments.
    [[nodiscard]] int cols() const noexcept { return cols_; }
    [[nodiscard]] int rows() const noexcept { return rows_; }
    [[nodiscard]] int cursor_col() const noexcept { return cursor_col_; }
    [[nodiscard]] int cursor_row() const noexcept { return cursor_row_; }
    [[nodiscard]] char cell(int col, int row) const noexcept;

    /// Codes arrive per events.hpp's text-host convention: printable keys as
    /// code points (shift already folded in by the host), Enter and
    /// Backspace by name, space as either its code point or its evdev
    /// number. Everything else is not this screen's business yet.
    void on_key_down(std::uint32_t code, visor::host::Modifiers modifiers) noexcept override;

    /// A screen takes typing, so Tab traversal may hand it the focus.
    [[nodiscard]] bool focusable() const noexcept override { return true; }

  protected:
    void on_draw(const visor::ui::Painter& painter) const noexcept override;

  private:
    /// The cells vector's index of one grid position.
    [[nodiscard]] std::size_t cell_index(int col, int row) const noexcept;

    /// Lands one printable byte under the cursor and steps; the step wraps.
    void put(unsigned char byte) noexcept;

    /// Steps to the next row, scrolling the grid up when the cursor already
    /// sits on the last one.
    void newline() noexcept;

    visor::draw::MonoFont font_;
    TerminalSource* source_ = nullptr;
    int cell_w_ = 1;
    int cell_h_ = 1;
    int cols_ = 0;
    int rows_ = 0;
    std::vector<char> cells_;
    int cursor_col_ = 0;
    int cursor_row_ = 0;
};

/**
 * @brief The D3 fake program: every byte sent comes straight back.
 *
 * send() answers inside the call by handing the bytes to the terminal's
 * receive(), so a keystroke paints itself -- the echo D3 judges, and the
 * shape D4 replaces with a PTY.
 */
class LoopbackSource final : public TerminalSource {
  public:
    explicit LoopbackSource(Terminal& echo) noexcept : echo_{&echo} {}

    void send(std::string_view bytes) noexcept override { echo_->receive(bytes); }

  private:
    Terminal* echo_;
};

} // namespace vesper::desktop
