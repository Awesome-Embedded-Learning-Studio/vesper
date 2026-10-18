#include "assets/pbm.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace vesper::assets {
namespace {

// Header whitespace skips blanks and whole # comment lines, per the Netpbm
// common header rules.
void skip_blanks_and_comments(std::span<const std::uint8_t> bytes, std::size_t& cursor) noexcept {
    while (cursor < bytes.size()) {
        const std::uint8_t byte = bytes[cursor];
        if (byte == '#') {
            while (cursor < bytes.size() && bytes[cursor] != '\n') {
                ++cursor;
            }
            continue;
        }
        const bool blank = byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n' ||
                           byte == '\v' || byte == '\f';
        if (!blank) {
            return;
        }
        ++cursor;
    }
}

// One dimension: decimal digits, refused when the running value outgrows the
// 65535 sanity cap before it can overflow size_t arithmetic.
[[nodiscard]] bool parse_dimension(std::span<const std::uint8_t> bytes, std::size_t& cursor,
                                   std::size_t& value, bool& absurd) noexcept {
    if (cursor >= bytes.size() || bytes[cursor] < '0' || bytes[cursor] > '9') {
        return false;
    }
    std::size_t parsed = 0;
    while (cursor < bytes.size() && bytes[cursor] >= '0' && bytes[cursor] <= '9') {
        parsed = (parsed * 10) + static_cast<std::size_t>(bytes[cursor] - '0');
        if (parsed > 65535) {
            absurd = true;
            return false;
        }
        ++cursor;
    }
    value = parsed;
    return true;
}

} // namespace

PbmImage decode_pbm(std::span<const std::uint8_t> bytes) noexcept {
    PbmImage image;
    if (bytes.size() < 2 || bytes[0] != 'P' || bytes[1] != '4') {
        image.error = PbmError::BadMagic;
        return image;
    }
    bool absurd = false;
    std::size_t cursor = 2;
    skip_blanks_and_comments(bytes, cursor);
    if (!parse_dimension(bytes, cursor, image.width, absurd)) {
        image.error = absurd ? PbmError::AbsurdDims : PbmError::BadHeader;
        return image;
    }
    skip_blanks_and_comments(bytes, cursor);
    if (!parse_dimension(bytes, cursor, image.height, absurd)) {
        image.error = absurd ? PbmError::AbsurdDims : PbmError::BadHeader;
        return image;
    }
    if (image.width == 0 || image.height == 0) {
        image.error = PbmError::AbsurdDims;
        return image;
    }
    // Exactly one whitespace byte closes the header, then the raster starts.
    if (cursor >= bytes.size()) {
        image.error = PbmError::Truncated;
        return image;
    }
    const std::uint8_t closing = bytes[cursor];
    const bool blank = closing == ' ' || closing == '\t' || closing == '\r' || closing == '\n' ||
                       closing == '\v' || closing == '\f';
    if (!blank) {
        image.error = PbmError::BadHeader;
        return image;
    }
    ++cursor;
    const std::size_t stride = (image.width + 7) / 8;
    const std::size_t needed = stride * image.height;
    if (bytes.size() - cursor < needed) {
        image.error = PbmError::Truncated;
        return image;
    }
    image.rows.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                      bytes.begin() + static_cast<std::ptrdiff_t>(cursor + needed));
    image.error = PbmError::Ok;
    image.ok = true;
    return image;
}

bool read_file_bytes(const char* path, std::vector<std::uint8_t>& out) noexcept {
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }
    out.clear();
    std::array<std::uint8_t, 4096> buffer{};
    while (true) {
        const std::size_t got = std::fread(buffer.data(), 1, buffer.size(), file);
        if (got > 0) {
            out.insert(out.end(), buffer.data(), buffer.data() + got);
        }
        if (got < buffer.size()) {
            const bool clean = std::ferror(file) == 0;
            const bool closed = std::fclose(file) == 0;
            return clean && closed;
        }
    }
}

} // namespace vesper::assets
