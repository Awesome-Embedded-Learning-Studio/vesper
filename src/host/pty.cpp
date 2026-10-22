#include "host/pty.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <asm-generic/ioctls.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// forkpty lives here; on glibc releases before 2.34 it rides in libutil.
#include <pty.h>

namespace vesper::host {
namespace {

/// Whether one inherited environ entry's name is one of the caller's
/// extras, so the entry is replaced rather than shadowed.
bool entry_is_replaced(const char* entry, std::span<const char* const> extra_env) noexcept {
    const std::string_view whole{entry};
    const std::string_view name = whole.substr(0, whole.find('='));
    return std::ranges::any_of(extra_env, [name](const char* extra) {
        const std::string_view other{extra};
        return other.substr(0, other.find('=')) == name;
    });
}

} // namespace

PtySession::PtySession(int master, int pid) noexcept : master_{master}, pid_{pid} {}

PtySession::~PtySession() noexcept {
    if (master_ < 0) {
        return;
    }
    // Closing the master hangs up the child's controlling terminal, so a
    // shell dies on SIGHUP instead of outliving its window; the wait then
    // returns promptly and no zombie is left behind.
    static_cast<void>(::close(master_));
    if (pid_ > 0) {
        static_cast<void>(::waitpid(pid_, nullptr, 0));
    }
}

PtySession::PtySession(PtySession&& other) noexcept
    : master_{other.master_}, pid_{other.pid_}, gone_{other.gone_} {
    other.master_ = -1;
    other.pid_ = -1;
    other.gone_ = false;
}

PtySession& PtySession::operator=(PtySession&& other) noexcept {
    if (this != &other) {
        this->~PtySession();
        master_ = other.master_;
        pid_ = other.pid_;
        gone_ = other.gone_;
        other.master_ = -1;
        other.pid_ = -1;
        other.gone_ = false;
    }
    return *this;
}

std::optional<PtySession> PtySession::open(const char* path, std::span<const char* const> args,
                                           std::span<const char* const> extra_env) noexcept {
    // exec's tables want char* and POSIX does not write through them; the
    // tables are assembled here so callers hand over plain literals.
    std::vector<char*> argv_table;
    argv_table.reserve(args.size() + 1);
    for (const char* const arg : args) {
        argv_table.push_back(const_cast<char*>(arg));
    }
    argv_table.push_back(nullptr);

    // The child inherits the caller's environment; an extra whose name
    // matches an inherited entry replaces it, because the getenv-visible
    // word must be the caller's newer one.
    std::vector<char*> env_table;
    env_table.reserve(extra_env.size() + 1);
    for (char* const* entry = ::environ; entry != nullptr && *entry != nullptr; ++entry) {
        if (!entry_is_replaced(*entry, extra_env)) {
            env_table.push_back(*entry);
        }
    }
    for (const char* const extra : extra_env) {
        env_table.push_back(const_cast<char*>(extra));
    }
    env_table.push_back(nullptr);

    // The borrowed core, CinuxOS's linux_spawn: forkpty drops the child on
    // a fresh controlling terminal and hands the parent the master.
    int master = -1;
    const pid_t pid = ::forkpty(&master, nullptr, nullptr, nullptr);
    if (pid < 0) {
        return std::nullopt;
    }
    if (pid == 0) {
        // Child: exec replaces us; 127 is the shell's number for a program
        // it could not run, kept so the failure reads the same everywhere.
        ::execvpe(path, argv_table.data(), env_table.data());
        ::_exit(127);
    }
    // Nonblocking master: a dry read is EAGAIN, never a stalled round.
    // The flag words are int by POSIX and the bitwise check wants unsigned,
    // so the OR happens one size up and lands back as int.
    const int flags = ::fcntl(master, F_GETFL, 0);
    if (flags >= 0) {
        const auto mixed = static_cast<unsigned>(flags) | static_cast<unsigned>(O_NONBLOCK);
        static_cast<void>(::fcntl(master, F_SETFL, static_cast<int>(mixed)));
    }
    return PtySession{master, static_cast<int>(pid)};
}

bool PtySession::set_winsize(int cols, int rows) const noexcept {
    if (master_ < 0 || cols <= 0 || rows <= 0) {
        return false;
    }
    ::winsize size{};
    size.ws_row = static_cast<unsigned short>(rows);
    size.ws_col = static_cast<unsigned short>(cols);
    return ::ioctl(master_, TIOCSWINSZ, &size) == 0;
}

bool PtySession::write_master(std::string_view bytes) noexcept {
    if (master_ < 0 || gone_) {
        return false;
    }
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const ssize_t wrote = ::write(master_, bytes.data() + sent, bytes.size() - sent);
        if (wrote > 0) {
            sent += static_cast<std::size_t>(wrote);
            continue;
        }
        if (wrote < 0 && (errno == EAGAIN || errno == EINTR)) {
            continue;
        }
        // A short-but-positive write with more to send loops; anything else
        // means the child side is finished with us.
        gone_ = true;
        return false;
    }
    return true;
}

int PtySession::read_master(std::uint8_t* buffer, int capacity) noexcept {
    if (master_ < 0 || gone_ || capacity <= 0) {
        return 0;
    }
    const ssize_t got = ::read(master_, buffer, static_cast<size_t>(capacity));
    if (got > 0) {
        return static_cast<int>(got);
    }
    if (got == 0) {
        gone_ = true;
        return 0;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return -1;
    }
    // EIO and friends are the pty master's way of saying the child side is
    // gone; every later call stays a quiet zero.
    gone_ = true;
    return 0;
}

} // namespace vesper::host
