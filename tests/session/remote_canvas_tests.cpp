#include "cases.hpp"
#include "testing.hpp"

#include "fake_color_host.hpp"
#include "session/wire.hpp"
#include "ui/remote_canvas/remote_canvas.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/events/key_words.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/bindings/events/pointer_words.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/canvas/pixel_traits.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/run_loop.hpp"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>

using vesper::host::FakeColorHost;

namespace {

using vesper::desktop::RemoteCanvas;
using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;

/// The child's end of the pair in the judgment's hand: nonblocking on both
/// halves the way the real spawn leaves them.
struct Channel {
    int desk = -1;
    int child = -1;

    Channel() {
        std::array<int, 2> pair{-1, -1};
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, pair.data()) == 0) {
            desk = pair[0];
            child = pair[1];
            // NOLINTNEXTLINE(bugprone-signed-bitwise): fcntl's flag words are ints.
            const int desk_nonblocking = ::fcntl(desk, F_GETFL, 0) | O_NONBLOCK;
            // NOLINTNEXTLINE(bugprone-signed-bitwise): same contract, other half.
            const int child_nonblocking = ::fcntl(child, F_GETFL, 0) | O_NONBLOCK;
            ::fcntl(desk, F_SETFL, desk_nonblocking);
            ::fcntl(child, F_SETFL, child_nonblocking);
        }
    }
    ~Channel() {
        if (desk >= 0) {
            ::close(desk);
        }
        if (child >= 0) {
            ::close(child);
        }
    }
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
};

/// One event word read off the child's end, or nullopt when nothing came.
[[nodiscard]] std::optional<vesper::session::EventBody> read_event(int fd) noexcept {
    std::array<std::uint8_t, sizeof(vesper::session::Header) + sizeof(vesper::session::EventBody)>
        line{};
    std::size_t got = 0;
    while (got < line.size()) {
        const auto n = ::read(fd, line.data() + got, line.size() - got);
        if (n <= 0) {
            return std::nullopt;
        }
        got += static_cast<std::size_t>(n);
    }
    const auto* header = reinterpret_cast<const vesper::session::Header*>(line.data());
    if (header->magic != vesper::session::kMagic || header->type != 10) {
        return std::nullopt;
    }
    return *reinterpret_cast<const vesper::session::EventBody*>(line.data() + sizeof(*header));
}

/// The exit witness: the handler flips it, the judgment reads it.
struct ExitWatch {
    RemoteCanvas* seen = nullptr;
    int calls = 0;
};

// NOLINTNEXTLINE(misc-const-correctness): the handler type carries void*.
void watch_exit(RemoteCanvas& self, void* context) noexcept {
    auto& watch = *static_cast<ExitWatch*>(context);
    watch.seen = &self;
    ++watch.calls;
}

constexpr RectI kFace{.pos = PointI{10, 20}, .size = SizeI{.w = 8, .h = 6}};
constexpr std::size_t kWords = 8UZ * 6UZ;

} // namespace

void run_remote_canvas_tests() {
    // One frame lands whole: the child's words ride the socket, pump()
    // decodes them into the image, and on_draw pastes them at the bounds
    // through the painter's image verb, pixels to match.
    {
        Channel channel;
        VESPER_CHECK(channel.desk >= 0);
        if (channel.desk < 0) {
            return;
        }
        std::array<std::uint32_t, kWords> frame{};
        for (std::size_t i = 0; i < kWords; ++i) {
            frame[i] = i % 2 == 0 ? 0xffc98a2dU : 0xff2b2620U;
        }
        constexpr auto line_bytes = sizeof(vesper::session::Header) + (kWords * 4U);
        std::array<std::uint8_t, line_bytes> line{};
        VESPER_CHECK(vesper::session::write_message(line.data(), line.size(),
                                                    vesper::session::Word::Frame, frame.data(),
                                                    static_cast<std::uint32_t>(kWords * 4U)));
        std::size_t sent = 0;
        while (sent < line.size()) {
            const auto n = ::write(channel.desk, line.data() + sent, line.size() - sent);
            if (n <= 0) {
                break;
            }
            sent += static_cast<std::size_t>(n);
        }
        VESPER_CHECK_EQ(sent, line.size());

        RemoteCanvas canvas{kFace, channel.child};
        channel.child = -1; // the canvas owns its end now
        canvas.pump();
        FakeColorHost host{64, 64};
        using ColorFrame =
            visor::canvas::Canvas<SizeI{.w = 64, .h = 64}, visor::canvas::Xrgb8888Traits>;
        ColorFrame out;
        visor::ui::InputRouter input;
        static_cast<void>(visor::ui::run_once(host.table(), canvas, input, out));
        VESPER_CHECK_EQ(host.get(PointI{10, 20}), 0xffc98a2dU);
        VESPER_CHECK_EQ(host.get(PointI{11, 20}), 0xff2b2620U);
        VESPER_CHECK_EQ(host.get(PointI{17, 25}), 0xff2b2620U); // the last cell, odd index
        VESPER_CHECK_EQ(host.get(PointI{18, 20}), 0U);          // past the face
    }

    // Input rides back: the router's key and pointer callbacks leave as
    // one event word each, the pointer position folded into the child's
    // own coordinates.
    {
        Channel channel;
        VESPER_CHECK(channel.desk >= 0);
        if (channel.desk < 0) {
            return;
        }
        RemoteCanvas canvas{kFace, channel.child};
        channel.child = -1;
        canvas.set_focused(true); // keys reach only the focused widget
        visor::ui::InputRouter input;
        visor::ui::dispatch_event(canvas, input,
                                  visor::host::Event{visor::host::KeyDown{
                                      .code = 30,
                                      .modifiers = visor::host::Modifiers{.ctrl = true},
                                  }},
                                  0, false);
        const auto key = read_event(channel.desk);
        VESPER_CHECK(key.has_value());
        if (key.has_value()) {
            VESPER_CHECK_EQ(key->kind, vesper::session::kKindKeyDown);
            VESPER_CHECK_EQ(key->code, 30U);
            VESPER_CHECK_EQ(key->modifiers, 2U); // ctrl is the second bit
        }
        visor::ui::dispatch_event(
            canvas, input,
            visor::host::Event{visor::host::PointerDown{.button = 1, .pos = PointI{12, 23}}}, 0,
            false);
        const auto pointer = read_event(channel.desk);
        VESPER_CHECK(pointer.has_value());
        if (pointer.has_value()) {
            VESPER_CHECK_EQ(pointer->kind, vesper::session::kKindPointerDown);
            VESPER_CHECK_EQ(pointer->x, 2); // 12 minus the face's origin
            VESPER_CHECK_EQ(pointer->y, 3);
        }
    }

    // The channel's ends: a Bye runs the exit handler once and the canvas
    // drops further work; the desk's own Close word crosses the other way.
    {
        Channel channel;
        VESPER_CHECK(channel.desk >= 0);
        if (channel.desk < 0) {
            return;
        }
        RemoteCanvas canvas{kFace, channel.child};
        channel.child = -1;
        ExitWatch watch;
        canvas.set_on_exit(&watch_exit, &watch);
        VESPER_CHECK(!canvas.child_gone());

        std::array<std::uint8_t, sizeof(vesper::session::Header)> bye{};
        VESPER_CHECK(vesper::session::write_message(bye.data(), bye.size(),
                                                    vesper::session::Word::Bye, nullptr, 0));
        [[maybe_unused]] const auto written = ::write(channel.desk, bye.data(), bye.size());
        canvas.pump();
        VESPER_CHECK(canvas.child_gone());
        VESPER_CHECK_EQ(watch.calls, 1);
        VESPER_CHECK_EQ(watch.seen, &canvas);
        canvas.pump(); // a gone canvas is quiet, and the handler never runs twice
        VESPER_CHECK_EQ(watch.calls, 1);
    }
    {
        Channel channel;
        VESPER_CHECK(channel.desk >= 0);
        if (channel.desk < 0) {
            return;
        }
        RemoteCanvas canvas{kFace, channel.child};
        channel.child = -1;
        canvas.send_close();
        std::array<std::uint8_t, sizeof(vesper::session::Header)> word{};
        std::size_t got = 0;
        while (got < word.size()) {
            const auto n = ::read(channel.desk, word.data() + got, word.size() - got);
            if (n <= 0) {
                break;
            }
            got += static_cast<std::size_t>(n);
        }
        const auto* header = reinterpret_cast<const vesper::session::Header*>(word.data());
        VESPER_CHECK_EQ(got, word.size());
        VESPER_CHECK_EQ(header->type, 11U); // the Close word's number
        VESPER_CHECK_EQ(header->length, 0U);
    }
}
