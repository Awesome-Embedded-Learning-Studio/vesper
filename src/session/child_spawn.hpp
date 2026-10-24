#pragma once

#include <optional>
#include <sys/types.h>

namespace vesper::session {

/// The desk's end of a freshly spawned child: the socket half and the pid
/// to reap when the channel ends. The fd is the caller's to hand to a
/// RemoteCanvas (or close on failure paths further down).
struct ChildChannel {
    int socket = -1;
    pid_t pid = -1;
};

/**
 * @brief Runs one program with the channel as its stdio.
 *
 * posix_spawn starts the child with the desk's socket half dup2'd onto
 * fds 0 and 1: the child's stdin and stdout are the private channel, the
 * Unix way a program becomes a pane (no new device, no new convention --
 * the same shape the pty lane gives bash). The child inherits no other
 * wiring; argv follows the exec convention, argv[0] naming the program.
 * Nullopt covers every failure with the cleanup already done.
 */
[[nodiscard]] std::optional<ChildChannel> spawn_child(const char* path, char* const* argv) noexcept;

} // namespace vesper::session
