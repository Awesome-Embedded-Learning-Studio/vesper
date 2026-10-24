// The D6a minimal child: a bouncing gold square on a warm grid, speaking
// the desk's private channel on its own stdio. It says Hello once, paints
// a frame every so often, echoes nothing, and leaves on the desk's Close
// word or a hangup -- the smallest program that can honestly claim to be
// windowed. The frame size is the construction-time agreement with the
// desk's example lane (200x120), kept here and there in the same words.
#include "session/wire.hpp"

#include <poll.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr int kWidth = 200;
constexpr int kHeight = 120;
constexpr std::size_t kFrameWords = static_cast<std::size_t>(kWidth) * kHeight;
constexpr int kFrameMillis = 120;
constexpr int kPollMillis = 40;

constexpr std::uint32_t kGround = 0xff2b'2620; // the desk's soft ink
constexpr std::uint32_t kGrid = 0xff3a'342c;   // one step lighter
constexpr std::uint32_t kSquare = 0xffc9'8a2d; // the desk's dusk gold
constexpr int kSquareSize = 24;

/// Writes one whole buffer, retrying the short writes a socket hands out.
bool write_all(const std::uint8_t* bytes, std::size_t count) noexcept {
    std::size_t sent = 0;
    while (sent < count) {
        const ssize_t wrote = ::write(1, bytes + sent, count - sent);
        if (wrote > 0) {
            sent += static_cast<std::size_t>(wrote);
            continue;
        }
        if (wrote < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        }
        return false;
    }
    return true;
}

bool send_hello() noexcept {
    vesper::session::HelloBody hello{};
    std::memcpy(hello.name, "block", 5);
    hello.width = static_cast<std::uint16_t>(kWidth);
    hello.height = static_cast<std::uint16_t>(kHeight);
    std::array<std::uint8_t, sizeof(vesper::session::Header) + sizeof(hello)> line{};
    if (!vesper::session::write_message(line.data(), line.size(), vesper::session::Word::Hello,
                                        &hello, sizeof(hello))) {
        return false;
    }
    return write_all(line.data(), line.size());
}

/// One frame of the show: the grid, and the square one step further along
/// its diagonal bounce (four steps of travel, one turn per edge).
void paint_frame(std::array<std::uint32_t, kFrameWords>& words, int tick) noexcept {
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const std::size_t at =
                (static_cast<std::size_t>(y) * kWidth) + static_cast<std::size_t>(x);
            words[at] = ((x % 20) == 0 || (y % 20) == 0) ? kGrid : kGround;
        }
    }
    // The bounce: position swings between 0 and span, flipping direction
    // at each end, the triangle wave a child can draw in its head.
    const int span_x = kWidth - kSquareSize;
    const int span_y = kHeight - kSquareSize;
    const int phase = tick % ((span_x + span_y) * 2);
    const int forward = phase < (span_x + span_y) ? phase : ((span_x + span_y) * 2) - phase;
    const int x = forward < span_x ? forward : span_x;
    const int y = forward < span_x ? 0 : forward - span_x;
    for (int row = 0; row < kSquareSize; ++row) {
        for (int column = 0; column < kSquareSize; ++column) {
            words[(static_cast<std::size_t>(y + row) * kWidth) +
                  static_cast<std::size_t>(x + column)] = kSquare;
        }
    }
}

bool send_frame(int tick) noexcept {
    std::array<std::uint32_t, kFrameWords> words{};
    paint_frame(words, tick);
    std::array<std::uint8_t, sizeof(vesper::session::Header) + (kFrameWords * 4U)> line{};
    if (!vesper::session::write_message(line.data(), line.size(), vesper::session::Word::Frame,
                                        words.data(),
                                        static_cast<std::uint32_t>(kFrameWords * 4U))) {
        return false;
    }
    return write_all(line.data(), line.size());
}

} // namespace

int main() {
    if (!send_hello()) {
        return 1;
    }
    std::array<std::uint8_t, 64> sink{};
    vesper::session::WireDecoder decoder{sink.data(), sink.size()};
    int tick = 0;
    int waited = 0;
    for (;;) {
        ::pollfd pfd{.fd = 0, .events = POLLIN, .revents = 0};
        const int ready = ::poll(&pfd, 1, kPollMillis);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 1;
        }
        if (ready > 0) {
            std::array<std::uint8_t, 4096> chunk{};
            const ssize_t got = ::read(0, chunk.data(), chunk.size());
            if (got == 0) {
                return 0; // the desk hung up
            }
            if (got > 0) {
                const auto arrived = static_cast<std::size_t>(got);
                std::size_t fed = 0;
                while (fed < arrived) {
                    const std::size_t used = decoder.feed(chunk.data() + fed, arrived - fed);
                    if (used == 0) {
                        break;
                    }
                    fed += used;
                    if (decoder.message_ready()) {
                        if (decoder.word() == vesper::session::Word::Close) {
                            return 0; // the desk asked us to leave
                        }
                        decoder.take();
                    }
                }
                if (decoder.broken()) {
                    return 1;
                }
            }
        }
        waited += kPollMillis;
        if (waited >= kFrameMillis) {
            waited = 0;
            if (!send_frame(tick)) {
                return 1;
            }
            ++tick;
        }
    }
}
