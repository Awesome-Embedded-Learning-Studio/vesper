#pragma once

#include <string_view>

#include "ui/style/style.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/bindings/events.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/widgets/button/button.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace vesper::desktop {

/**
 * @brief The window: a title band that drags, a close box that reclaims the
 *        frame, and a content area holding one widget.
 *
 * The frame is a composite in visor's tree: the band and the outline are the
 * frame's own pixels (drawn in on_draw, hit-tested by the frame itself),
 * while the close box is a real Button child and the content area holds one
 * adopted widget. A press on the band therefore reaches the frame through
 * ordinary hit testing -- the close box and the content intercept their own
 * presses first -- and visor's capture semantics keep every following move
 * and the release coming to the frame wherever the pointer goes. The drag is
 * that capture plus one subtraction: each move re-derives the frame's origin
 * from the grab offset.
 *
 * The reporting discipline is inherited, not reimplemented: every bounds
 * change rides Widget::set_bounds, which reports the old and the new box to
 * the dirty sink, so a drag never leaves stale pixels or unclaimed ones.
 * Bounds go through set_position (or the constructor); the inherited
 * set_bounds would move the frame and strand its children.
 *
 * Two states, painted, not inferred: an active frame wears the accent band,
 * an inactive one the plain ground. The desktop says which is which through
 * set_active; the frame does not reach for a focus manager it does not own.
 */
class WindowFrame final : public visor::ui::Widget {
  public:
    /// @brief What a press on the frame calls, before the drag logic: the
    ///        desktop's cue to make this frame the active one.
    using FocusHandler = void (*)(WindowFrame& self, void* context);

    /// The band's height, border rows included; the title font rides
    /// centered inside it.
    static constexpr int kTitleBarHeight = style::kTitleBarHeight;

    /// The close box, a square outlined button in the band's right corner.
    static constexpr int kCloseBoxSize = style::kCloseBoxSize;

    /// desktop_bounds is the frame's leash: drags are clamped so the frame
    /// never leaves that rectangle. A frame that could leave the desktop
    /// could strand its title band out of reach, and the band is the only
    /// handle this frame offers.
    WindowFrame(visor::base::RectI bounds, std::string_view title,
                const visor::draw::MonoFont& font, visor::base::RectI desktop_bounds) noexcept;

    /// Adopts the close box. Call once after construction; false means the
    /// child table is full, which a frame holding only its close box cannot
    /// reach.
    [[nodiscard]] bool initialize() noexcept;

    /// Adopts the one widget shown in the content area; false on a second
    /// call or a null widget. The widget's lifetime stays with the caller,
    /// the same stance the tree takes everywhere.
    [[nodiscard]] bool set_content(visor::ui::Widget* content) noexcept;

    /// The band, in canvas coordinates: full width, kTitleBarHeight tall.
    [[nodiscard]] visor::base::RectI title_bar() const noexcept;

    /// The rectangle below the band where the content widget lives.
    [[nodiscard]] visor::base::RectI content_area() const noexcept;

    /// The close box's rectangle, exposed for judgments that click it.
    [[nodiscard]] visor::base::RectI close_box() const noexcept;

    /// Moves the frame's top-left corner, keeping its size. The band, the
    /// close box and the content follow through their own set_bounds calls,
    /// each reporting its old and new box; the frame's own two reports cover
    /// them all, the rest is thrift for the renderer's strip replay.
    void set_position(visor::base::PointI pos) noexcept;

    /// Resizes the frame in place, the position kept (W2's follow): the
    /// band keeps its height and the children re-seat through the same
    /// layout walk set_position takes, so a face that grew or shrank never
    /// strands its content.
    void reshape(visor::base::SizeI size) noexcept;

    /// Takes the window off the screen. The visibility flip reports the
    /// frame's box on its own, so the ground shows through where the window
    /// stood; state and content stay as they are for a later re-showing.
    /// What the close box asks when pressed. The default answer is the
    /// plain hide of close(); a desk with a child behind the window wires
    /// set_on_close and answers with its own goodbye -- the desk closes
    /// the channel, the channel's end closes the frame.
    using CloseHandler = void (*)(WindowFrame& self, void* context);

    void set_on_close(CloseHandler handler, void* context = nullptr) noexcept;

    void close() noexcept;

    /// Wires the press cue; the handler runs before the band's drag starts.
    void set_on_focus(FocusHandler handler, void* context = nullptr) noexcept;

    /// The band's ink state. A no-op when the state already matches, so a
    /// desktop may call it on every press without buying repaints.
    void set_active(bool on) noexcept;

    [[nodiscard]] bool active() const noexcept { return active_; }

    void on_pointer_down(visor::base::PointI at,
                         visor::host::Modifiers modifiers) noexcept override;
    void on_pointer_move(visor::base::PointI at) noexcept override;
    void on_pointer_up(visor::base::PointI at) noexcept override;

  protected:
    void on_draw(const visor::ui::Painter& painter) const noexcept override;

  private:
    /// Re-derives the close box and the content rectangle from the frame's
    /// bounds. The frame is a small layout manager: bounds change only
    /// through the constructor and set_position, and every change walks
    /// through here, so the children can never strand.
    void layout_children() noexcept;

    /// The close box's activation lands here; Button hands back the context
    /// word given to set_on_activate, which is this frame.
    static void on_close_activated(visor::ui::Button& self, void* context) noexcept;

    std::string_view title_;
    visor::draw::MonoFont font_;
    visor::ui::Button close_;
    visor::ui::Widget* content_ = nullptr;
    visor::base::RectI desktop_bounds_;
    visor::base::PointI drag_offset_;
    FocusHandler on_focus_ = nullptr;
    void* focus_context_ = nullptr;
    CloseHandler on_close_ = nullptr;
    void* close_context_ = nullptr;
    bool dragging_ = false;
    bool active_ = true;
};

} // namespace vesper::desktop
