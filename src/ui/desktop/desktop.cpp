#include "ui/desktop/desktop.hpp"

#include "ui/style/style.hpp"

#include <algorithm>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_bitmap.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/widget.hpp"

#include <string_view>

namespace vesper::desktop {

using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
using visor::canvas::ColorRole;

Desktop::Desktop(RectI bounds, std::string_view banner, const visor::draw::MonoFont& font,
                 const visor::ui::Theme& theme) noexcept
    : Widget{bounds},
      // The name rides the panel, centered on its line: the panel strip is
      // the desk's one piece of permanent chrome, so the label is a child
      // of the desk whose ground the panel already painted.
      banner_{
          PointI{bounds.pos.x + style::kContentPadding,
                 bounds.pos.y + bounds.size.h - style::kPanelHeight +
                     std::max((style::kPanelHeight - static_cast<int>(font.line_height)) / 2, 0)},
          banner, font, ColorRole::Background} {
    set_theme(&theme);
    // The label is a member and the child table starts empty, so the
    // adoption cannot fail; the result is folded away at the one call site.
    static_cast<void>(add_child(&banner_));
}

const visor::ui::Label& Desktop::banner() const noexcept {
    return banner_;
}

RectI Desktop::panel_bounds() const noexcept {
    return RectI{
        .pos = PointI{bounds().pos.x, bounds().pos.y + bounds().size.h - style::kPanelHeight},
        .size = SizeI{.w = bounds().size.w, .h = style::kPanelHeight},
    };
}

void Desktop::set_wallpaper(const visor::draw::MonoBitmap* art) noexcept {
    wallpaper_ = art;
    // The ground pixels change with the picture, so the whole desk is owed;
    // the window boxes above repaint on top of it in the same walk.
    invalidate(bounds());
}

void Desktop::on_draw(const visor::ui::Painter& painter) const noexcept {
    painter.fill(bounds(), ColorRole::Background);
    if (wallpaper_ != nullptr) {
        painter.draw_bitmap(bounds().pos, *wallpaper_, ColorRole::Foreground, 1);
    }
    // The panel is the foreground ink, so the banner label (drawn as a
    // child on top) takes the background role and reads as a cut-out.
    painter.fill(panel_bounds(), ColorRole::Foreground);
}

} // namespace vesper::desktop
