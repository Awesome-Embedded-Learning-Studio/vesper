#pragma once

// The desk's color scheme: one place where the seven roles take their real
// values, beside the layout numbers style.hpp already owns. The first color
// face keeps the light-paper polarity the mono desk had -- ink on paper --
// and gives the title band and the icon art their first real colors. The
// palette is the desk's own name: Vesper is the evening star, so the accent
// is dusk gold over warm paper. A mono canvas folds this theme back to ink
// polarity (every non-black RGB is ink), so the same table still drives the
// fbdev lane untouched.

#include "visor/canvas/color.hpp"
#include "visor/ui/theme.hpp"

namespace vesper::desktop::style {

inline constexpr visor::ui::Theme kColorDeskTheme{
    .background = visor::canvas::Ink::from_rgb(0xf6f2ea), // warm paper
    .foreground = visor::canvas::Ink::from_rgb(0x2b2620), // soft ink
    .muted = visor::canvas::Ink::from_rgb(0x8d8578),      // caption gray
    .accent = visor::canvas::Ink::from_rgb(0xc98a2d),     // dusk gold
    .on_accent = visor::canvas::Ink::from_rgb(0x1f1a12),  // dark ink on gold
    .border = visor::canvas::Ink::from_rgb(0xc9c1b2),     // paper border
    .disabled = visor::canvas::Ink::from_rgb(0xb3aca0),   // faded paper
    .corner_radius =
        visor::ui::CornerRadius{
            .top_left = 2,
            .top_right = 2,
            .bottom_right = 2,
            .bottom_left = 2,
        },
};

} // namespace vesper::desktop::style
