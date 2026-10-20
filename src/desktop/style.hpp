#pragma once

// One place for the desktop's layout numbers. Widgets read them instead of
// carrying their own; a future density change edits this file, not five
// draw calls. Values are in canvas pixels (the X11 lane doubles them).
//
// Most values snap to the four-pixel unit. The two-pixel ones are the close
// box's inset and the caption gap: optical corrections inside a box that
// already fills its band.
namespace vesper::desktop::style {

/// The title band's full height, border rows included.
inline constexpr int kTitleBarHeight = 16;

/// The close box, a square whose border and label ride the title font.
inline constexpr int kCloseBoxSize = 12;

/// The air between the title band and the content, and between the content
/// and the frame's side and bottom borders.
inline constexpr int kContentPadding = 4;

/// The title's distance from the frame's left edge.
inline constexpr int kTitleInset = 8;

/// The close box's distance from the frame's top and right edges.
inline constexpr int kCloseInset = 2;

/// The bottom strip that carries the desk's name and status.
inline constexpr int kPanelHeight = 20;

/// One icon's hit target: art, caption and the breathing room around them.
inline constexpr int kIconSlotWidth = 56;
inline constexpr int kIconSlotHeight = 56;

/// The gap between an icon's art and its caption.
inline constexpr int kCaptionGap = 2;

} // namespace vesper::desktop::style
