#pragma once

#include <string_view>

#include "ui/style/style.hpp"

#include "visor/base/geometry/rect.hpp"
#include "visor/draw/mono_bitmap.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/label/label.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace vesper::desktop {

/**
 * @brief The desktop root: one full-screen box that paints the ground, a
 *        panel along the bottom edge, and the floating windows above both.
 *
 * The desktop is an ordinary Widget in visor's tree, so the platform rules
 * are inherited, not reinvented: children draw in add order, hit testing
 * walks them backwards, and a window added after the panel floats above
 * everything added earlier. The panel is the one always-on piece of chrome:
 * a full-width strip in the foreground ink carrying the desk's name label
 * on the left. Callers may anchor their own right-aligned label inside
 * panel_bounds() for a status word.
 */
class Desktop final : public visor::ui::Widget {
  public:
    /// The theme pointer must outlive the tree, the same line every visor
    /// host example draws; the font storage outlives the banner label for
    /// the same reason.
    Desktop(visor::base::RectI bounds, std::string_view banner, const visor::draw::MonoFont& font,
            const visor::ui::Theme& theme) noexcept;

    /// The banner line, exposed for judgments that locate the panel's text.
    [[nodiscard]] const visor::ui::Label& banner() const noexcept;

    /// The bottom strip's rectangle: full width, style::kPanelHeight tall.
    [[nodiscard]] visor::base::RectI panel_bounds() const noexcept;

    /// Sets the desk's picture, painted once at the desktop's origin under
    /// the panel and everything the children add. A null pointer (or never
    /// calling) keeps the plain ground fill. The bitmap is a borrowed view:
    /// the storage (usually a decoded PbmImage) must outlive the desktop,
    /// the same line the theme and font pointers draw. Parts reaching past
    /// the desktop's box clip silently.
    void set_wallpaper(const visor::draw::MonoBitmap* art) noexcept;

  protected:
    void on_draw(const visor::ui::Painter& painter) const noexcept override;

  private:
    visor::ui::Label banner_;
    const visor::draw::MonoBitmap* wallpaper_ = nullptr;
};

} // namespace vesper::desktop
