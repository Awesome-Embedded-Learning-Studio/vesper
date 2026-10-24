#pragma once

#include "compositor/seat.hpp"
#include "compositor/shell.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/bindings/events/pointer_words.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace vesper::desktop {

/**
 * @brief One Wayland guest's face on the desk: the widget half of the C1
 *        judgment, RemoteCanvas's shape wearing a ShellSurface instead of
 *        a private channel.
 *
 * The widget owns nothing of the guest: on_draw replays whatever the
 * surface currently holds through the painter's image verb, exactly the
 * discipline every other widget draws under, and note_commit() is the
 * desk's post-dispatch step that turns a guest's commit into repaint debt.
 * The debt is the whole widget for now; damage-driven granularity is the
 * frame chapter's (C4) to tighten.
 *
 * The input half (C2) is RemoteCanvas's shape on the seat's wire: every
 * word the router delivers rides down to the guest, positions folded into
 * the surface's own coordinates first, and the pointer's arrival carries
 * focus with it. The keyboard's focus is the desk's window layer to aim
 * (C3's job), not this widget's.
 */
class WaylandCanvas final : public visor::ui::Widget {
  public:
    /// The shell surface whose pixels this canvas shows and the seat whose
    /// wire the input rides (a null seat keeps the widget display-only);
    /// both pointers are borrowed for the widget's whole life.
    WaylandCanvas(visor::base::RectI bounds, compositor::ShellSurface* shell,
                  compositor::Seat* seat = nullptr) noexcept;

    /// The desk's word after a dispatch round: whatever the guest last
    /// committed owes a repaint. The whole face, for the rounds that
    /// re-aimed the window or drew it first.
    void note_commit() noexcept;

    /// The same word with the guest's own damage report folded in: only
    /// the changed rectangle owes pixels (surface-local coordinates, empty
    /// reads as the whole face, out-of-bounds parts clip silently).
    void note_damage(visor::base::RectI surface_damage) noexcept;

    // The input overrides stay public the way RemoteCanvas's do: the
    // router calls them through the widget interface, and hiding them
    // would only promise an encapsulation the tree never asked for.
    void on_key_down(std::uint32_t code, visor::host::Modifiers modifiers) noexcept override;
    void on_key_up(std::uint32_t code, visor::host::Modifiers modifiers) noexcept override;
    void on_pointer_down(visor::base::PointI at,
                         visor::host::Modifiers modifiers) noexcept override;
    void on_pointer_up(visor::base::PointI at) noexcept override;
    void on_pointer_move(visor::base::PointI at) noexcept override;
    void on_pointer_wheel(const visor::host::PointerWheel& wheel) noexcept override;
    [[nodiscard]] bool focusable() const noexcept override;

  protected:
    void on_draw(const visor::ui::Painter& painter) const noexcept override;

  private:
    /// The position folded into the surface's own coordinates.
    [[nodiscard]] visor::base::PointI local(visor::base::PointI at) const noexcept;

    compositor::ShellSurface* shell_;
    compositor::Seat* seat_;
};

} // namespace vesper::desktop
