#include "cases.hpp"
#include "testing.hpp"

#include "base/libpbm/pbm.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace {

using vesper::assets::decode_pbm;
using vesper::assets::PbmError;
using vesper::assets::read_file_bytes;

// "P4\n8 2\n" plus raster 0xAA 0x55: two rows of eight, alternating ink.
const std::array<std::uint8_t, 7> kGood{
    'P', '4', '\n', '8', ' ', '2', '\n',
};
const std::array<std::uint8_t, 2> kGoodTail{0xAA, 0x55};

std::vector<std::uint8_t> as_bytes(const std::span<const std::uint8_t>& head,
                                   const std::span<const std::uint8_t>& tail) {
    std::vector<std::uint8_t> bytes(head.begin(), head.end());
    bytes.insert(bytes.end(), tail.begin(), tail.end());
    return bytes;
}

} // namespace

void run_pbm_tests() {
    // The good file: dims read, raster stored as packed, and the bitmap view
    // reads pixels MSB first. 0xAA lights x0 of row 0, 0x55 lights x1 of
    // row 1.
    {
        const std::vector<std::uint8_t> bytes = as_bytes(kGood, kGoodTail);
        const auto image = decode_pbm(bytes);
        VESPER_CHECK(image.ok);
        VESPER_CHECK(image.error == PbmError::Ok);
        VESPER_CHECK_EQ(image.width, std::size_t{8});
        VESPER_CHECK_EQ(image.height, std::size_t{2});
        VESPER_CHECK_EQ(image.rows.size(), std::size_t{2});
        VESPER_CHECK(image.bitmap().pixel(0, 0));
        VESPER_CHECK(!image.bitmap().pixel(1, 0));
        VESPER_CHECK(!image.bitmap().pixel(0, 1));
        VESPER_CHECK(image.bitmap().pixel(1, 1));
    }

    // Comments ride anywhere in the header, per the common format. 0xF0 on a
    // 4-wide row lights x0..x3, the padding tail reads off through the
    // width guard.
    {
        const std::array<std::uint8_t, 15> commented{
            'P', '4', '\n', '#', ' ', 'c', '\n', '4', ' ', '2', '\n', 0xF0, 0x0F, 0x00, 0x00,
        };
        const auto image = decode_pbm(commented);
        VESPER_CHECK(image.ok);
        VESPER_CHECK_EQ(image.width, std::size_t{4});
        VESPER_CHECK_EQ(image.height, std::size_t{2});
        VESPER_CHECK_EQ(image.rows.size(), std::size_t{2}); // one packed byte per row
        VESPER_CHECK(image.bitmap().pixel(3, 0));
        VESPER_CHECK(!image.bitmap().pixel(0, 1));
    }

    // A raster shorter than the dims promise is truncated, never padded.
    {
        const std::array<std::uint8_t, 1> tail{0xAA};
        const std::vector<std::uint8_t> bytes = as_bytes(kGood, tail);
        const auto image = decode_pbm(bytes);
        VESPER_CHECK(!image.ok);
        VESPER_CHECK(image.error == PbmError::Truncated);
        VESPER_CHECK(image.rows.empty());
    }

    // Wrong magic is refused before any parsing: P5 is the gray PGM, not ours.
    {
        const std::array<std::uint8_t, 10> gray{
            'P', '5', '\n', '8', ' ', '2', '\n', 0x00, 0x01, 0x02,
        };
        const auto image = decode_pbm(gray);
        VESPER_CHECK(!image.ok);
        VESPER_CHECK(image.error == PbmError::BadMagic);
        const std::array<std::uint8_t, 1> lone{'P'};
        VESPER_CHECK(decode_pbm(lone).error == PbmError::BadMagic);
    }

    // A header with no numbers, or one not closed by a whitespace byte, is
    // refused as a bad header.
    {
        const std::array<std::uint8_t, 7> no_digits{'P', '4', '\n', 'a', 'b', '\n', 0x00};
        VESPER_CHECK(decode_pbm(no_digits).error == PbmError::BadHeader);
        const std::array<std::uint8_t, 11> no_close{
            'P', '4', '\n', '8', ' ', '2', 'X', 0xAA, 0x55, 0xAA, 0x55,
        };
        VESPER_CHECK(decode_pbm(no_close).error == PbmError::BadHeader);
    }

    // Absurd dims are refused before any size math: a dimension of zero, or
    // one beyond the 65535 sanity cap.
    {
        const std::array<std::uint8_t, 9> zero_w{'P', '4', '\n', '0', ' ', '2', '\n', 0x00, 0x00};
        VESPER_CHECK(decode_pbm(zero_w).error == PbmError::AbsurdDims);
        const std::array<std::uint8_t, 12> huge_w{
            'P', '4', '\n', '7', '0', '0', '0', '0', ' ', '1', '\n', 0x00,
        };
        VESPER_CHECK(decode_pbm(huge_w).error == PbmError::AbsurdDims);
    }

    // The file shovel: bytes written to disk come back identical and decode
    // clean, and a missing path reads false without touching the buffer.
    {
        const char* path = "/tmp/vesper_pbm_roundtrip.pbm";
        const std::vector<std::uint8_t> bytes = as_bytes(kGood, kGoodTail);
        std::FILE* file = std::fopen(path, "wb");
        VESPER_CHECK(file != nullptr);
        if (file != nullptr) {
            VESPER_CHECK_EQ(std::fwrite(bytes.data(), 1, bytes.size(), file), bytes.size());
            VESPER_CHECK_EQ(std::fclose(file), 0);
        }
        std::vector<std::uint8_t> read_back;
        VESPER_CHECK(read_file_bytes(path, read_back));
        VESPER_CHECK_EQ(read_back.size(), bytes.size());
        VESPER_CHECK(read_back == bytes);
        VESPER_CHECK(decode_pbm(read_back).ok);
        VESPER_CHECK(std::remove(path) == 0);
        std::vector<std::uint8_t> untouched{0x77};
        VESPER_CHECK(!read_file_bytes("/tmp/vesper_pbm_no_such_file.pbm", untouched));
        VESPER_CHECK_EQ(untouched.size(), std::size_t{1});
    }
}
