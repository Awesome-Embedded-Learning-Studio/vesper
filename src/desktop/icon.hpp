#pragma once

#include <string_view>

#include "visor/base/geometry/rect.hpp"
#include "visor/bindings/events.hpp"
#include "visor/draw/mono_bitmap.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace vesper::desktop {

/**
 * @brief One desktop icon: a bitmap over a caption, opened by a double click.
 *
 * The icon paints only ink -- the art block and the caption line -- and no
 * ground of its own: the desktop's Background is the icon's ground, so an
 * icon reads as sitting on the desk rather than as a box on it. The one
 * exception is the selected state: a rounded ring around the slot, drawn
 * before the ink, which is the only time the icon claims a ground.
 *
 * It takes no children. The caption is painted here, not hosted in a Label,
 * because hit_test() returns the deepest child and a Label child would
 * swallow the presses over the caption, splitting one icon into two hit
 * targets and breaking the router's same-widget pairing for the gesture.
 *
 * The open gesture is the router's double click (its time window and slop,
 * both visor's), arriving as on_double_click. The reaction is a plain
 * function plus a context word, the vocabulary Button's activation takes:
 * the desktop wires what opening means, the icon does not know windows
 * exist. The press cue is separate on purpose: selection happens on the
 * first click of the pair, opening only when the pair completes.
 */
class Icon final : public visor::ui::Widget {
  public:
    /// @brief What a completed double click calls. context is the word
    ///        handed to set_on_activate, given back untouched.
    using ActivateHandler = void (*)(Icon& self, void* context);

    /// @brief What a press calls, before the click pair completes: the
    ///        desktop's cue to move the selection here.
    using SelectHandler = void (*)(Icon& self, void* context);

    /// bounds is the whole hit target. The art block and the caption line
    /// ride centered inside it, as one block: art on top, caption directly
    /// underneath.
    Icon(visor::base::RectI bounds, const visor::draw::MonoBitmap& art, std::string_view caption,
         const visor::draw::MonoFont& font) noexcept;

    /// Wires the open reaction. An icon with no handler still paints its
    /// ink; a double click simply fires into the quiet, the stance an
    /// unwired Button takes.
    void set_on_activate(ActivateHandler handler, void* context = nullptr) noexcept;

    /// Wires the selection cue; runs on every press over the icon.
    void set_on_select(SelectHandler handler, void* context = nullptr) noexcept;

    /// The selected look: a rounded ring around the slot. No-op when the
    /// state already matches.
    void set_selected(bool on) noexcept;
    [[nodiscard]] bool selected() const noexcept { return selected_; }

    void on_pointer_down(visor::base::PointI at,
                         visor::host::Modifiers modifiers) noexcept override;
    void on_double_click(visor::base::PointI at,
                         visor::host::Modifiers modifiers) noexcept override;

  protected:
    void on_draw(const visor::ui::Painter& painter) const noexcept override;

  private:
    const visor::draw::MonoBitmap* art_;
    visor::draw::MonoFont font_;
    std::string_view caption_;
    ActivateHandler on_activate_ = nullptr;
    void* activate_context_ = nullptr;
    SelectHandler on_select_ = nullptr;
    void* select_context_ = nullptr;
    bool selected_ = false;
};

} // namespace vesper::desktop
