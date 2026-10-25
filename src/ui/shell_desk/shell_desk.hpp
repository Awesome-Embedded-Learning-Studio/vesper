#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "compositor/server.hpp"
#include "compositor/shell.hpp"
#include "compositor/xwayland.hpp"

#include "ui/desktop/desktop.hpp"
#include "ui/wayland_canvas/wayland_canvas.hpp"
#include "ui/window_frame/window_frame.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/draw/mono_font.hpp"

namespace vesper::desktop {

/**
 * @brief The desk's window layer for Wayland guests (the roadmap's C3):
 *        every mapped toplevel becomes a framed window on the desktop.
 *
 * The desk owns nothing but the wiring: on_map frames the guest's face at
 * the next cascade seat, on_unmap takes the window down again, and the
 * close button asks the guest first -- a Wayland window closes when its
 * guest says so, never by the desk's own hand. Focus is the one desk-side
 * privilege: clicking a window (frame, band or face) activates it, raises
 * it, and aims the seat's keyboard at its guest, one window at a time.
 *
 * One pump() per round keeps the pixels honest: the server serves its
 * guests, every new commit becomes its canvas's repaint debt, and the
 * frame callbacks leave with the flush. Drag, layering and capture are
 * inherited, not reinvented -- the frame's band drags, add order stacks,
 * and the router's capture is the pointer grab the protocol implies.
 */
class ShellDesk {
  public:
    static constexpr std::size_t kWindowCapacity = 8;

    /// The desktop hosts the windows and the server feeds them; the font
    /// titles the frames. Both are borrowed for the desk's whole life.
    ShellDesk(Desktop& desktop, compositor::Server& server,
              const visor::draw::MonoFont& title_font) noexcept;
    ~ShellDesk();

    ShellDesk(const ShellDesk&) = delete;
    ShellDesk& operator=(const ShellDesk&) = delete;

    /// One round between the host's polls: serve the guests, then -- after
    /// whatever words landed -- collect their commits as repaint debt. The
    /// desk renders between the two halves and presents after.
    void pump() noexcept;

    /// The debt half alone: whatever the guests committed since the last
    /// walk becomes its canvas's repaint, the guest's own damage report
    /// naming the rectangle.
    void collect() noexcept;

    /// The round's focus endgame, called after the render round dispatched
    /// the pointer's words: whatever guest face the pointer last entered
    /// takes the desk's focus.
    void settle_focus() noexcept;

    /// The desk's presentation word, called once after the round's render:
    /// the frame callbacks fire on this beat and the consumed buffers go
    /// home. The desk that renders once per poll presents once per poll --
    /// the cadence the guests' frame rates follow.
    void present() noexcept;

    /// The desk's eye moving elsewhere: a local window (an icon's terminal,
    /// an about box) took the press, so every guest's band goes dark and
    /// the seat's keyboard lets go. The tree's focus is not touched -- the
    /// local window seated whoever it seated.
    void release_focus() noexcept;

    /// The framed window a shell surface wears, or null for a stranger;
    /// the judgments walk it to find where a guest's pixels sit.
    [[nodiscard]] const WindowFrame* frame_of(const compositor::ShellSurface* shell) const noexcept;

    /// The number of windows standing.
    [[nodiscard]] std::size_t window_count() const noexcept;

    /// Wires the XWayland bridge: its wm wire is pumped with the rounds
    /// and its faces' close words route through it.
    void set_xwayland(compositor::XWayland* xwayland) noexcept;

  private:
    struct Slot {
        compositor::ShellSurface* shell = nullptr;
        WindowFrame* frame = nullptr;
        WaylandCanvas* canvas = nullptr;
        std::uint64_t epoch = 0;
        bool focused = false;
    };

    static void on_shell_map(compositor::ShellSurface& shell, void* context) noexcept;
    static void on_shell_unmap(compositor::ShellSurface& shell, void* context) noexcept;
    static void on_frame_focus(WindowFrame& frame, void* context) noexcept;
    static void on_frame_close(WindowFrame& frame, void* context) noexcept;
    /// The corner drag's word: the wanted face clamped into the desktop,
    /// then the resize ask the guest answers with its next commit.
    static void on_face_resize(WaylandCanvas& canvas, visor::base::SizeI wanted,
                               void* context) noexcept;

    void handle_map(compositor::ShellSurface& shell) noexcept;
    void handle_unmap(compositor::ShellSurface& shell) noexcept;
    void focus_slot(Slot& slot) noexcept;
    void defocus_all() noexcept;
    /// The focused guest's share of the shared tree: a keyboard pressed
    /// into the frame's own chrome (a band, the close button) is called
    /// back to the canvas, while a keyboard a local window holds is that
    /// window's share -- the guests step aside whole instead of stealing.
    void reclaim_tree_focus(Slot& slot) noexcept;
    /// Whether target sits in node's subtree; the shared-tree judgment the
    /// reclaim decision walks. visor keeps parent pointers private, so the
    /// walk goes down the children instead of up the parents.
    [[nodiscard]] static bool within(visor::ui::Widget* node,
                                     const visor::ui::Widget* target) noexcept;
    /// The topmost standing window, for the handoff a closing focused
    /// window owes its survivors.
    [[nodiscard]] Slot* topmost_slot() noexcept;
    [[nodiscard]] Slot* slot_of(const compositor::ShellSurface* shell) noexcept;
    [[nodiscard]] Slot* slot_of_frame(const WindowFrame& frame) noexcept;
    [[nodiscard]] Slot* slot_of_canvas(const WaylandCanvas& canvas) noexcept;
    void drop_slot(Slot& slot) noexcept;

    Desktop& desktop_;
    compositor::Server& server_;
    const visor::draw::MonoFont& title_font_;
    compositor::XWayland* xwayland_ = nullptr;
    std::array<Slot, kWindowCapacity> slots_{};
    int placed_ = 0; // the cascade's counter, one seat per window ever placed
};

} // namespace vesper::desktop
