#include "ui/window_frame/window_frame.hpp"

#include "ui/style/style.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/base/helpers/rect_helper.hpp"
#include "visor/bindings/events.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/text.hpp"
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/widget.hpp"
#include <algorithm>
#include <cstdint>
#include <string_view>

namespace vesper::desktop {

using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
using visor::canvas::ColorRole;
using visor::ui::CornerRadius;

namespace {

/// Shrinks each corner, so an inset fill keeps the outer silhouette one
/// pixel thick instead of letting its own corners poke past it.
CornerRadius inset_corners(CornerRadius radius, int by) noexcept {
    const auto shrink = [by](std::uint8_t value) {
        return static_cast<std::uint8_t>(std::max(static_cast<int>(value) - by, 0));
    };
    return CornerRadius{
        .top_left = shrink(radius.top_left),
        .top_right = shrink(radius.top_right),
        .bottom_right = shrink(radius.bottom_right),
        .bottom_left = shrink(radius.bottom_left),
    };
}

/// The close box's home: a square tucked into the band's right corner.
RectI close_box_for(RectI frame) noexcept {
    return RectI{
        .pos = PointI{frame.pos.x + frame.size.w - style::kCloseInset - style::kCloseBoxSize,
                      frame.pos.y + style::kCloseInset},
        .size = {.w = style::kCloseBoxSize, .h = style::kCloseBoxSize},
    };
}

} // namespace

WindowFrame::WindowFrame(RectI bounds, std::string_view title, const visor::draw::MonoFont& font,
                         RectI desktop_bounds) noexcept
    : Widget{bounds}, title_{title}, font_{font}, close_{close_box_for(bounds)},
      desktop_bounds_{desktop_bounds} {
    // Outlined on purpose: a filled button paints in Accent, the same ink
    // the active band wears, and would vanish into it. The hollow box with
    // the title font's X reads on both band states.
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
    // The content sits under the band with one padding unit of air on every
    // side, the frame's one-pixel border included. The band owns the top
    // row, so the content's top pad measures from the band's bottom edge.
    const PointI origin{bounds().pos.x + 1 + style::kContentPadding,
                        bounds().pos.y + kTitleBarHeight + style::kContentPadding};
    const SizeI size{
        .w = bounds().size.w - 2 - (2 * style::kContentPadding),
        .h = bounds().size.h - kTitleBarHeight - 1 - (2 * style::kContentPadding),
    };
    return RectI{.pos = origin, .size = size};
}

RectI WindowFrame::close_box() const noexcept {
    return close_box_for(bounds());
}

void WindowFrame::set_on_focus(FocusHandler handler, void* context) noexcept {
    on_focus_ = handler;
    focus_context_ = context;
}

void WindowFrame::set_active(bool on) noexcept {
    if (active_ == on) {
        return;
    }
    active_ = on;
    // The band's ink is the only change; the content and the close box do
    // not know about the state.
    invalidate(bounds());
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
    // The press reached the frame itself, so the desktop's cue fires before
    // the band test: clicking the border or the band hands this frame the
    // keyboard, the way a window manager focuses on any press.
    if (on_focus_ != nullptr) {
        on_focus_(*this, focus_context_);
    }
    // Only a press on the band starts a drag; the close box and the content
    // take their own presses before this handler. The band test is the
    // frame's own judgment of where the press landed -- a press on the
    // border pixels drags too, which reads as grabbing the frame.
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
    // One-pixel silhouette in Border, face in Background, band in Accent
    // when active and in the plain face when not. The children (close box,
    // content) paint on top of this ground. Rounding comes from the theme's
    // corner radius, so a scheme with square corners paints square windows.
    const CornerRadius outer = theme() != nullptr ? theme()->corner_radius : CornerRadius{};
    const CornerRadius inner = inset_corners(outer, 1);
    painter.fill_rounded(bounds(), ColorRole::Border, outer);
    painter.fill_rounded(visor::base::inset(bounds(), 1), ColorRole::Background, inner);

    const RectI band = visor::base::inset(title_bar(), 1);
    painter.fill_rounded(band, active_ ? ColorRole::Accent : ColorRole::Background, inner);

    const int title_y =
        band.pos.y + std::max((band.size.h - static_cast<int>(font_.line_height)) / 2, 0);
    const PointI at{bounds().pos.x + style::kTitleInset, title_y};
    visor::ui::draw_text_clipped(painter, band, at, font_, title_,
                                 active_ ? ColorRole::OnAccent : ColorRole::Foreground, 1);
}

} // namespace vesper::desktop
