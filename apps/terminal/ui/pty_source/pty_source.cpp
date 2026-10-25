#include "ui/pty_source/pty_source.hpp"

#include "host/pty.hpp"
#include "ui/terminal/terminal.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <poll.h>
#include <string_view>

namespace vesper::desktop {

PtySource::PtySource(Terminal& term, vesper::host::PtySession& session) noexcept
    : term_{&term}, session_{&session} {}

void PtySource::send(std::string_view bytes) noexcept {
    static_cast<void>(session_->write_master(bytes));
}

bool PtySource::child_gone() const noexcept {
    return session_->child_gone();
}

bool PtySource::pump(int timeout_ms) noexcept {
    if (session_->child_gone()) {
        if (!farewell_shown_) {
            farewell_shown_ = true;
            term_->receive("\n(exited)");
        }
        return false;
    }
    struct pollfd waiter;
    waiter.fd = session_->master_fd();
    waiter.events = POLLIN;
    waiter.revents = 0;
    if (::poll(&waiter, 1, timeout_ms) <= 0) {
        return false;
    }
    bool any = false;
    std::array<std::uint8_t, 256> chunk{};
    while (true) {
        const int got = session_->read_master(chunk.data(), static_cast<int>(chunk.size()));
        if (got <= 0) {
            break; // dry (minus one) or the child side gone (zero)
        }
        term_->receive(std::string_view{reinterpret_cast<const char*>(chunk.data()),
                                        static_cast<std::size_t>(got)});
        any = true;
        if (static_cast<std::size_t>(got) < chunk.size()) {
            break; // short read: the pipe gave all it had this wake
        }
    }
    if (session_->child_gone() && !farewell_shown_) {
        farewell_shown_ = true;
        term_->receive("\n(exited)");
    }
    return any;
}

} // namespace vesper::desktop
