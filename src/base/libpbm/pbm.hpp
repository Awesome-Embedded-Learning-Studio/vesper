#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "visor/draw/mono_bitmap.hpp"

namespace vesper::assets {

/// What went wrong, for the caller's one line of diagnostics.
enum class PbmError : std::uint8_t {
    Ok,
    BadMagic,   ///< not a P4 file
    BadHeader,  ///< dims missing, not numbers, or header not closed by one whitespace
    Truncated,  ///< header parsed, raster shorter than dims promise
    AbsurdDims, ///< zero or absurdly large dims, refused before any math overflows
};

/**
 * @brief One decoded P4 (binary portable bitmap) image.
 *
 * The raster is stored exactly as the file packed it: row-major, MSB first,
 * (width + 7) / 8 bytes per row. That is the canvas packing, so the rows wrap
 * into a visor MonoBitmap with no repacking -- a P4 file is a mono screen's
 * native dish.
 */
struct PbmImage {
    bool ok = false;
    PbmError error = PbmError::BadMagic;
    std::size_t width = 0;
    std::size_t height = 0;
    std::vector<std::uint8_t> rows;

    /// A read-only view over the raster. The view borrows this image's
    /// storage: keep the PbmImage alive as long as the bitmap is drawn.
    [[nodiscard]] visor::draw::MonoBitmap bitmap() const noexcept {
        return visor::draw::MonoBitmap{.width = width, .height = height, .rows = rows};
    }
};

/// Decodes P4 bytes. Comments (# to end of line) are accepted anywhere the
/// format's common header allows them. The raster must carry exactly the
/// bytes the dims promise; trailing junk beyond it is ignored.
[[nodiscard]] PbmImage decode_pbm(std::span<const std::uint8_t> bytes) noexcept;

/// Reads one whole file as bytes; false on any open or read failure, leaving
/// out empty. The desktop is a userland program and may touch the filesystem,
/// a liberty the borrowed visor tree deliberately does not take.
[[nodiscard]] bool read_file_bytes(const char* path, std::vector<std::uint8_t>& out) noexcept;

} // namespace vesper::assets
