#pragma once

#include <string_view>

#include "visor/base/geometry/rect.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/label/label.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace vesper::desktop {

/**
 * @brief The desktop root: one full-screen box that paints the ground and
 *        carries the banner line and the floating windows above it.
 *
 * The desktop is an ordinary Widget in visor's tree, so the platform rules
 * are inherited, not reinvented: children draw in add order, hit testing
 * walks them backwards, and a window added after the banner floats above
 * everything added earlier. D0 keeps the surface to the ground fill and one
 * banner line along the bottom edge; the window frames arrive in D1 as
 * further children.
 */
class Desktop final : public visor::ui::Widget {
  public:
    /// The theme pointer must outlive the tree, the same line every visor
    /// host example draws; the font storage outlives the banner label for
    /// the same reason.
    Desktop(visor::base::RectI bounds, std::string_view banner, const visor::draw::MonoFont& font,
            const visor::ui::Theme& theme) noexcept;

    /// The banner line, exposed for judgments that locate the one area ink
    /// is allowed in.
    [[nodiscard]] const visor::ui::Label& banner() const noexcept;

  protected:
    void on_draw(const visor::ui::Painter& painter) const noexcept override;

  private:
    visor::ui::Label banner_;
};

} // namespace vesper::desktop
