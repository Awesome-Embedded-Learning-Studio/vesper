#include "desktop/desktop.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_bitmap.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/widget.hpp"
#include <string_view>

namespace vesper::desktop {

Desktop::Desktop(visor::base::RectI bounds, std::string_view banner,
                 const visor::draw::MonoFont& font, const visor::ui::Theme& theme) noexcept
    : Widget{bounds},
      // The banner rides the bottom edge, one font line tall with a margin
      // on both sides; a bottom line reads as the desk's name plate and
      // leaves the upper ground to the windows that arrive later.
      banner_{visor::base::PointI{bounds.pos.x + 6, bounds.pos.y + bounds.size.h -
                                                        static_cast<int>(font.line_height) - 6},
              banner, font} {
    set_theme(&theme);
    // The label is a member and the child table starts empty, so the
    // adoption cannot fail; the result is folded away at the one call site.
    static_cast<void>(add_child(&banner_));
}

const visor::ui::Label& Desktop::banner() const noexcept {
    return banner_;
}

void Desktop::set_wallpaper(const visor::draw::MonoBitmap* art) noexcept {
    wallpaper_ = art;
    // The ground pixels change with the picture, so the whole desk is owed;
    // the window boxes above repaint on top of it in the same walk.
    invalidate(bounds());
}

void Desktop::on_draw(const visor::ui::Painter& painter) const noexcept {
    painter.fill(bounds(), visor::canvas::ColorRole::Background);
    if (wallpaper_ != nullptr) {
        painter.draw_bitmap(bounds().pos, *wallpaper_, visor::canvas::ColorRole::Foreground, 1);
    }
}

} // namespace vesper::desktop
