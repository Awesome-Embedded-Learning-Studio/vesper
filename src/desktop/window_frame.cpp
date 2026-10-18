#include "desktop/window_frame.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/helpers/rect_helper.hpp"
#include "visor/bindings/events.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/text.hpp"
#include "visor/ui/widgets/widget.hpp"
#include <algorithm>
#include <string_view>

namespace vesper::desktop {

using visor::base::PointI;
using visor::base::RectI;

WindowFrame::WindowFrame(RectI bounds, std::string_view title, const visor::draw::MonoFont& font,
                         RectI desktop_bounds) noexcept
    : Widget{bounds}, title_{title}, font_{font},
      close_{RectI{
          .pos = PointI{bounds.pos.x + bounds.size.w - kCloseBoxSize - 2, bounds.pos.y + 1},
          .size = {.w = kCloseBoxSize, .h = kCloseBoxSize},
      }},
      desktop_bounds_{desktop_bounds} {
    // Outlined on purpose: a filled button paints in Accent, the same ink
    // the band wears, and would vanish into it. The hollow box with the X
    // reads on both states.
    close_.set_outlined(true);
    close_.set_label("X", font_);
    // The close box fires into this frame: the handler's context word is the
    // frame itself, the plain-pointer vocabulary Button's activation takes.
    close_.set_on_activate(&WindowFrame::on_close_activated, this);
}

void WindowFrame::on_close_activated([[maybe_unused]] visor::ui::Button& self,
                                     void* context) noexcept {
    static_cast<WindowFrame*>(context)->close();
}

bool WindowFrame::initialize() noexcept {
    return add_child(&close_);
}

bool WindowFrame::set_content(visor::ui::Widget* content) noexcept {
    if (content == nullptr || content_ != nullptr) {
        return false;
    }
    if (!add_child(content)) {
        return false;
    }
    content_ = content;
    layout_children();
    return true;
}

RectI WindowFrame::title_bar() const noexcept {
    return RectI{.pos = bounds().pos, .size = {.w = bounds().size.w, .h = kTitleBarHeight}};
}

RectI WindowFrame::content_area() const noexcept {
    return RectI{
        .pos = PointI{bounds().pos.x + 1, bounds().pos.y + 1 + kTitleBarHeight},
        .size = {.w = bounds().size.w - 2, .h = bounds().size.h - 2 - kTitleBarHeight},
    };
}

RectI WindowFrame::close_box() const noexcept {
    return RectI{
        .pos = PointI{bounds().pos.x + bounds().size.w - kCloseBoxSize - 2, bounds().pos.y + 1},
        .size = {.w = kCloseBoxSize, .h = kCloseBoxSize},
    };
}

void WindowFrame::set_position(PointI pos) noexcept {
    if (pos == bounds().pos) {
        return;
    }
    // Widget::set_bounds reports the old and the new box; the frame adds no
    // reporting of its own. The children then walk through the same method,
    // their reports landing inside the frame's two boxes.
    set_bounds(RectI{.pos = pos, .size = bounds().size});
    layout_children();
}

void WindowFrame::close() noexcept {
    dragging_ = false;
    set_visible(false);
}

void WindowFrame::layout_children() noexcept {
    close_.set_bounds(close_box());
    if (content_ != nullptr) {
        content_->set_bounds(content_area());
    }
}

void WindowFrame::on_pointer_down([[maybe_unused]] PointI at,
                                  [[maybe_unused]] visor::host::Modifiers modifiers) noexcept {
    // Only a press that hit testing actually routed here starts a drag; the
    // close box and the content take their own presses first. The band test
    // is the frame's own judgment of where the press landed -- a press on
    // the border pixels drags too, which reads as grabbing the frame.
    if (!title_bar().contains(at)) {
        return;
    }
    dragging_ = true;
    drag_offset_ = PointI{at.x - bounds().pos.x, at.y - bounds().pos.y};
}

void WindowFrame::on_pointer_move(PointI at) noexcept {
    // Capture keeps the moves coming while the press is held, wherever the
    // pointer goes; without a held press this is a hover visit and changes
    // nothing. The leash clamps the drag into the desktop: a frame that
    // could leave it could strand its title band out of reach.
    if (!dragging_) {
        return;
    }
    PointI next{at.x - drag_offset_.x, at.y - drag_offset_.y};
    const int slack_x = std::max(desktop_bounds_.size.w - bounds().size.w, 0);
    const int slack_y = std::max(desktop_bounds_.size.h - bounds().size.h, 0);
    next.x = std::clamp(next.x, desktop_bounds_.pos.x, desktop_bounds_.pos.x + slack_x);
    next.y = std::clamp(next.y, desktop_bounds_.pos.y, desktop_bounds_.pos.y + slack_y);
    set_position(next);
}

void WindowFrame::on_pointer_up([[maybe_unused]] PointI at) noexcept {
    dragging_ = false;
}

void WindowFrame::on_draw(const visor::ui::Painter& painter) const noexcept {
    // One-pixel silhouette in Border, face in Background, band in Accent;
    // the children (close box, content) paint on top of this ground.
    painter.fill(bounds(), visor::canvas::ColorRole::Border);
    painter.fill(visor::base::inset(bounds(), 1), visor::canvas::ColorRole::Background);
    painter.fill(title_bar(), visor::canvas::ColorRole::Accent);
    const PointI at{bounds().pos.x + 3,
                    bounds().pos.y + ((kTitleBarHeight - static_cast<int>(font_.line_height)) / 2)};
    visor::ui::draw_text_clipped(painter, title_bar(), at, font_, title_,
                                 visor::canvas::ColorRole::OnAccent, 1);
}

} // namespace vesper::desktop
