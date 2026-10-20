#include "desktop/icon.hpp"

#include "desktop/style.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_bitmap.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/text.hpp"
#include "visor/ui/widgets/widget.hpp"

#include <string_view>

namespace vesper::desktop {

using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
using visor::canvas::ColorRole;

Icon::Icon(RectI bounds, const visor::draw::MonoBitmap& art, std::string_view caption,
           const visor::draw::MonoFont& font) noexcept
    : Widget{bounds}, art_{&art}, font_{font}, caption_{caption} {}

void Icon::set_on_activate(ActivateHandler handler, void* context) noexcept {
    on_activate_ = handler;
    activate_context_ = context;
}

void Icon::set_on_select(SelectHandler handler, void* context) noexcept {
    on_select_ = handler;
    select_context_ = context;
}

void Icon::set_selected(bool on) noexcept {
    if (selected_ == on) {
        return;
    }
    selected_ = on;
    invalidate(bounds());
}

void Icon::on_pointer_down([[maybe_unused]] PointI at,
                           [[maybe_unused]] visor::host::Modifiers modifiers) noexcept {
    if (on_select_ != nullptr) {
        on_select_(*this, select_context_);
    }
}

void Icon::on_double_click(PointI at, visor::host::Modifiers modifiers) noexcept {
    static_cast<void>(at);
    static_cast<void>(modifiers);
    if (on_activate_ != nullptr) {
        on_activate_(*this, activate_context_);
    }
}

void Icon::on_draw(const visor::ui::Painter& painter) const noexcept {
    // One centered block: the art with the caption line under it. The block
    // height rides the art plus the caption gap plus one font line, so the
    // label sits close under the art the way a nameplate does. Everything
    // is Foreground ink on the desktop's ground; parts outside the box clip
    // silently, and a caption wider than the box centers to a negative
    // offset that the clip trims to nothing on the leading side.
    const int art_w = static_cast<int>(art_->width);
    const int art_h = static_cast<int>(art_->height);
    const int caption_h = static_cast<int>(font_.line_height);
    const int block_h = art_h + style::kCaptionGap + caption_h;
    const int block_top = bounds().pos.y + ((bounds().size.h - block_h) / 2);

    if (selected_) {
        // A one-pixel ring, not a plate: four edge runs keep the desk and
        // any wallpaper visible inside the slot. A filled plate would cut a
        // black hole in the picture under the icon.
        const RectI box = bounds();
        const SizeI top{.w = box.size.w, .h = 1};
        painter.fill(RectI{.pos = box.pos, .size = top}, ColorRole::Border);
        painter.fill(RectI{.pos = PointI{box.pos.x, box.pos.y + box.size.h - 1}, .size = top},
                     ColorRole::Border);
        const SizeI side{.w = 1, .h = box.size.h};
        painter.fill(RectI{.pos = box.pos, .size = side}, ColorRole::Border);
        painter.fill(RectI{.pos = PointI{box.pos.x + box.size.w - 1, box.pos.y}, .size = side},
                     ColorRole::Border);
    }

    const int art_x = bounds().pos.x + ((bounds().size.w - art_w) / 2);
    painter.draw_bitmap(PointI{art_x, block_top}, *art_, ColorRole::Foreground, 1);

    const visor::base::SizeI caption_size = visor::draw::measure_text(font_, caption_, 1);
    const int caption_x = bounds().pos.x + ((bounds().size.w - caption_size.w) / 2);
    visor::ui::draw_text_clipped(painter, bounds(),
                                 PointI{caption_x, block_top + art_h + style::kCaptionGap}, font_,
                                 caption_, ColorRole::Foreground, 1);
}

} // namespace vesper::desktop
