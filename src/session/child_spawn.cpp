#include "session/child_spawn.hpp"

#include "visor/bindings/host.hpp"
#include "visor/bindings/spawn.hpp"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <sys/types.h>

#include <array>
#include <cstddef>
#include <optional>
#include <span>

namespace vesper::session {

std::optional<ChildChannel> spawn_child(const visor::host::HostTable& birth, const char* path,
                                        char* const* argv) noexcept {
    std::array<int, 2> pair{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, pair.data()) != 0) {
        return std::nullopt;
    }
    // The desk's half does not block: the desk polls between frames, and a
    // blocking read here would hand the child a vote on the loop's rhythm.
    const int desk_flags = ::fcntl(pair[0], F_GETFL, 0);
    // NOLINTNEXTLINE(bugprone-signed-bitwise): fcntl's flag words are ints by contract.
    const int nonblocking = desk_flags | O_NONBLOCK;
    if (desk_flags < 0 || ::fcntl(pair[0], F_SETFL, nonblocking) != 0) {
        ::close(pair[0]);
        ::close(pair[1]);
        return std::nullopt;
    }

    // The fourth slot does the birth: both stdio numbers land on the
    // pair's child half, the environment rides whole, and no whitelist
    // closes anything -- the lane's child is a zero-dependency program
    // that inherits the desk's world.
    const std::array<visor::host::FdGift, 2> gifts{
        visor::host::FdGift{.child_fd = 0, .parent_fd = pair[1]},
        visor::host::FdGift{.child_fd = 1, .parent_fd = pair[1]},
    };
    // argv arrives null-terminated (the exec convention); the request
    // speaks spans, so the words are counted once here.
    std::size_t words = 0;
    while (argv[words] != nullptr) {
        ++words;
    }
    const visor::host::SpawnRequest request{
        .path = path,
        .argv = std::span<const char* const>(argv, words),
        .env = {},
        .gifts = gifts,
        .close_other_fds = false,
    };
    const int child = birth.spawn_child(request);
    ::close(pair[1]); // the child holds its own duplicate now
    if (child < 0) {
        ::close(pair[0]);
        return std::nullopt;
    }
    return ChildChannel{.socket = pair[0], .pid = static_cast<pid_t>(child)};
}

} // namespace vesper::session
