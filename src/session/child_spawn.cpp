#include "session/child_spawn.hpp"

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <unistd.h>

#include <sys/types.h>

#include <array>
#include <optional>

namespace vesper::session {

std::optional<ChildChannel> spawn_child(const char* path, char* const* argv) noexcept {
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

    posix_spawn_file_actions_t actions;
    if (::posix_spawn_file_actions_init(&actions) != 0) {
        ::close(pair[0]);
        ::close(pair[1]);
        return std::nullopt;
    }
    // The child's stdio is the other half of the pair, both directions on
    // the same descriptor; CLOEXEC never applies here because the whole
    // point is that the exec'd program keeps this fd.
    ::posix_spawn_file_actions_adddup2(&actions, pair[1], 0);
    ::posix_spawn_file_actions_adddup2(&actions, pair[1], 1);

    pid_t child_pid = -1;
    const int spawned = ::posix_spawn(&child_pid, path, &actions, nullptr, argv, nullptr);
    ::posix_spawn_file_actions_destroy(&actions);
    ::close(pair[1]); // the child holds its own duplicate now
    if (spawned != 0) {
        ::close(pair[0]);
        return std::nullopt;
    }
    return ChildChannel{.socket = pair[0], .pid = child_pid};
}

} // namespace vesper::session
