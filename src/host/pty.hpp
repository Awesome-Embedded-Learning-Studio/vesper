#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace vesper::host {

/**
 * @brief A child program running under a pseudo terminal, one fd both ways.
 *
 * The spawn is borrowed whole from CinuxOS (libs/gui/host/posix_spawn.cpp,
 * its linux_spawn: forkpty + exec), recorded in PIN.md. forkpty gives the
 * child a controlling terminal, so a shell gets its line editing and every
 * byte it writes -- including the pty's own echo of what the desktop sends
 * -- arrives back on the master. The master is the program's one door:
 * write_master() is the keyboard, read_master() is the output.
 *
 * The master is set nonblocking at open, so a dry read answers EAGAIN
 * instead of stalling the caller's round. A read that reports the child
 * side gone (EOF or EIO, the numbers a pty master hands back when the
 * session leader dies) flips child_gone() and every later call stays a
 * quiet zero.
 */
class PtySession {
  public:
    PtySession() noexcept = default;
    ~PtySession() noexcept;

    // A session is an open fd and a child pid, an identity not a value.
    PtySession(const PtySession&) = delete;
    PtySession& operator=(const PtySession&) = delete;
    PtySession(PtySession&& other) noexcept;
    PtySession& operator=(PtySession&& other) noexcept;

    /// Runs path with args (the exec convention: args[0] is the program's
    /// own name) under a fresh pty. The child inherits the caller's
    /// environment plus extra_env, whose entries are NAME=VALUE strings; an
    /// extra whose name matches an inherited entry replaces it, so the
    /// child sees one word per name. Nullopt when forkpty fails; a failed
    /// exec is reported by the child exiting with 127, not by this call.
    [[nodiscard]] static std::optional<PtySession>
    open(const char* path, std::span<const char* const> args,
         std::span<const char* const> extra_env = {}) noexcept;

    /// Keystrokes toward the child. False when the write went short or the
    /// child side is already gone; the dropped bytes are the program's to
    /// retype, the stance a full pipe deserves.
    [[nodiscard]] bool write_master(std::string_view bytes) noexcept;

    /// One nonblocking read from the master. Positive is the byte count;
    /// zero means the child side is gone; minus one means nothing right
    /// now.
    int read_master(std::uint8_t* buffer, int capacity) noexcept;

    /// The master's descriptor, for the caller's poll().
    [[nodiscard]] int master_fd() const noexcept { return master_; }

    /// Tells the child how wide and tall its terminal is, in cells. Without
    /// this a line editor guesses a default width and redraws its line
    /// against the wrong wrap, which a screen of a different width shows as
    /// misplaced prompts and half-drawn rows. The kernel hands the change to
    /// the child as SIGWINCH, the line editor's own repaint cue. False when
    /// the ioctl failed, a dead session's answer.
    [[nodiscard]] bool set_winsize(int cols, int rows) const noexcept;

    /// Whether the child side has reported itself gone.
    [[nodiscard]] bool child_gone() const noexcept { return gone_; }

  private:
    PtySession(int master, int pid) noexcept;

    int master_ = -1;
    int pid_ = -1;
    bool gone_ = false;
};

} // namespace vesper::host
