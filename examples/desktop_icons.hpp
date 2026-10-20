#pragma once

// The desk's two icons, 24x24, drawn for the eyeball lane: a window with a V
// in its face and a terminal with a prompt over a stand. One byte per eight
// pixels, MSB first, three bytes per row -- the canvas packing. The comment
// beside each row is the picture itself, so a change is a diff of dots.
//
// The art is deliberately one-pixel stroke: the desk paints at 2x, and a
// one-pixel line is the crispest a 1bpp bitmap gets after the host's doubled
// pixels. Filled shapes would read as blobs at this size.

#include <array>
#include <cstdint>

#include "visor/draw/mono_bitmap.hpp"

namespace vesper::examples {

inline constexpr std::array<std::uint8_t, 72> kWindowArtBits{
    0x00, 0x00, 0x00, // ........................
    0x00, 0x00, 0x00, // ........................
    0x1F, 0xFF, 0xF8, // ...##################...
    0x20, 0x00, 0x04, // ..#..................#..
    0x20, 0x00, 0x34, // ..#...............##.#..
    0x20, 0x00, 0x34, // ..#...............##.#..
    0x20, 0x00, 0x04, // ..#..................#..
    0x3F, 0xFF, 0xFC, // ..####################..
    0x20, 0x00, 0x04, // ..#..................#..
    0x20, 0x00, 0x04, // ..#..................#..
    0x20, 0x00, 0x04, // ..#..................#..
    0x21, 0x00, 0x44, // ..#....#.........#...#..
    0x20, 0x80, 0x84, // ..#.....#.......#....#..
    0x20, 0x80, 0x84, // ..#.....#.......#....#..
    0x20, 0x41, 0x04, // ..#......#.....#.....#..
    0x20, 0x22, 0x04, // ..#.......#...#......#..
    0x20, 0x14, 0x04, // ..#........#.#.......#..
    0x20, 0x14, 0x04, // ..#........#.#.......#..
    0x20, 0x08, 0x04, // ..#.........#........#..
    0x20, 0x00, 0x04, // ..#..................#..
    0x20, 0x00, 0x04, // ..#..................#..
    0x1F, 0xFF, 0xF8, // ...##################...
    0x00, 0x00, 0x00, // ........................
    0x00, 0x00, 0x00, // ........................
};

inline constexpr std::array<std::uint8_t, 72> kTermArtBits{
    0x00, 0x00, 0x00, // ........................
    0x00, 0x00, 0x00, // ........................
    0x00, 0x00, 0x00, // ........................
    0x00, 0x00, 0x00, // ........................
    0x1F, 0xFF, 0xF8, // ...##################...
    0x20, 0x00, 0x04, // ..#..................#..
    0x20, 0x00, 0x04, // ..#..................#..
    0x20, 0x00, 0x04, // ..#..................#..
    0x22, 0x00, 0x04, // ..#...#..............#..
    0x21, 0x00, 0x04, // ..#....#.............#..
    0x20, 0x80, 0x04, // ..#.....#............#..
    0x21, 0x1F, 0x04, // ..#....#...#####.....#..
    0x22, 0x1F, 0x04, // ..#...#....#####.....#..
    0x20, 0x00, 0x04, // ..#..................#..
    0x20, 0x00, 0x04, // ..#..................#..
    0x20, 0x00, 0x04, // ..#..................#..
    0x20, 0x00, 0x04, // ..#..................#..
    0x20, 0x00, 0x04, // ..#..................#..
    0x1F, 0xFF, 0xF8, // ...##################...
    0x00, 0x3C, 0x00, // ..........####..........
    0x00, 0x3C, 0x00, // ..........####..........
    0x00, 0xFF, 0x00, // ........########........
    0x00, 0x00, 0x00, // ........................
    0x00, 0x00, 0x00, // ........................
};

inline constexpr visor::draw::MonoBitmap kWindowArt{
    .width = 24,
    .height = 24,
    .rows = kWindowArtBits,
};

inline constexpr visor::draw::MonoBitmap kTermArt{
    .width = 24,
    .height = 24,
    .rows = kTermArtBits,
};

} // namespace vesper::examples
