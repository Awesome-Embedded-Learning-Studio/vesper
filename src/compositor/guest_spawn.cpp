#include "compositor/guest_spawn.hpp"

#include "compositor/server.hpp"
#include "visor/bindings/host.hpp"

#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>
#include <span>
#include <sys/types.h>

#include <wayland-util.h>

// POSIX and libwayland C interop: the checker's symbol-to-header maps do
// not know the sys/wait and wayland-util spellings this file lives on, the
// same exemption the XWayland bridge draws.
// NOLINTBEGIN(misc-include-cleaner)

#include <sys/socket.h>
#include <sys/wait.h>

#include <wayland-server-core.h>

#include "visor/bindings/spawn.hpp"

namespace vesper::compositor {
namespace {

/// The desk environment's word budget, kept in step with the POSIX filling's
/// own: a caller environment fatter than this disarms the birth rather than
/// silently slimming the child's world.
constexpr std::size_t kEnvWordBudget = 256;

/// Whether one inherited environ entry is a word this birth owns (the stale
/// outer wayland words the fresh connection must replace, never stack on).
bool entry_is_replaced(const char* entry) noexcept {
    return std::strncmp(entry, "WAYLAND_SOCKET=", 15) == 0 ||
           std::strncmp(entry, "WAYLAND_DISPLAY=", 16) == 0;
}

} // namespace

/// The hang-up notice and the guest it belongs to, one allocation.
struct GuestSpawn::DestroyNotice {
    wl_listener link{};
    GuestSpawn* owner = nullptr;
};

GuestSpawn::GuestSpawn() noexcept = default;

GuestSpawn::~GuestSpawn() {
    shutdown_child();
}

bool GuestSpawn::alive() const noexcept {
    if (child_ <= 0) {
        return false;
    }
    const pid_t result = ::waitpid(child_, nullptr, WNOHANG);
    if (result == child_ || (result < 0 && errno == ECHILD)) {
        child_ = -1;
        return false;
    }
    return result == 0 || (result < 0 && errno == EINTR);
}

wl_client* GuestSpawn::client() const noexcept {
    return client_;
}

void GuestSpawn::client_gone(wl_listener* listener, void* /*data*/) noexcept {
    // The macro's typeof reads the declared name, so the explicit type
    // stays; auto here would ask the initializer for its own type.
    // NOLINTNEXTLINE(modernize-use-auto)
    GuestSpawn::DestroyNotice* notice = wl_container_of(listener, notice, link);
    notice->owner->client_ = nullptr;
    notice->owner->notice_ = nullptr; // the notice frees itself below
    wl_list_remove(&notice->link.link);
    delete notice;
}

void GuestSpawn::shutdown_child() noexcept {
    if (notice_ != nullptr) {
        wl_list_remove(&notice_->link.link);
        delete notice_;
        notice_ = nullptr;
    }
    if (child_ > 0) {
        // The goodbye the bridge taught: a term, a bounded grace, then the
        // certain word, and the wait that leaves nothing behind.
        static_cast<void>(::kill(child_, SIGTERM));
        for (int waited = 0; waited < 50; ++waited) {
            if (::waitpid(child_, nullptr, WNOHANG) == child_ || errno == ECHILD) {
                child_ = -1;
                break;
            }
            static_cast<void>(::usleep(10'000));
        }
        if (child_ > 0) {
            static_cast<void>(::kill(child_, SIGKILL));
            while (::waitpid(child_, nullptr, 0) < 0 && errno == EINTR) {
            }
            child_ = -1;
        }
    }
    if (client_ != nullptr) {
        wl_client_destroy(client_);
        client_ = nullptr;
    }
}

bool GuestSpawn::launch(Server& server, const visor::host::HostTable& table, const char* path,
                        std::span<const char* const> argv) noexcept {
    if (client_ != nullptr || alive() || !server.valid() || !table.can_spawn() || path == nullptr) {
        return false;
    }
    if (::access(path, X_OK) != 0) {
        return false;
    }

    std::array<int, 2> pair{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair.data()) != 0) {
        return false;
    }

    // The child's one wiring word: its guest connection at the number it
    // already holds, the same hand Xwayland receives its WAYLAND_SOCKET
    // from. Stale outer wayland words are skipped so the fresh connection
    // is the only one the child sees.
    std::array<char, 24> socket_word{};
    static_cast<void>(
        std::snprintf(socket_word.data(), socket_word.size(), "WAYLAND_SOCKET=%d", pair[1]));
    std::array<const char*, kEnvWordBudget + 1> env_words{};
    std::size_t env_count = 0;
    for (char* const* word = ::environ; *word != nullptr; ++word) {
        if (entry_is_replaced(*word)) {
            continue;
        }
        if (env_count == kEnvWordBudget) {
            ::close(pair[0]);
            ::close(pair[1]);
            return false;
        }
        env_words[env_count] = *word;
        ++env_count;
    }
    env_words[env_count] = socket_word.data();

    // The pass-as-is gift: the child keeps the number, the desk adopts the
    // other half, and the whitelist stance closes everything else above
    // stdio so the program cannot reach back into the desk's channels.
    const std::array<visor::host::FdGift, 1> gifts{
        visor::host::FdGift{.child_fd = pair[1], .parent_fd = pair[1]},
    };
    const visor::host::SpawnRequest request{
        .path = path,
        .argv = argv,
        .env = std::span<const char* const>(env_words.data(), env_count + 1),
        .gifts = gifts,
        .close_other_fds = true,
    };
    const int child = table.spawn_child(request);
    if (child < 0) {
        ::close(pair[0]);
        ::close(pair[1]);
        return false;
    }
    child_ = static_cast<pid_t>(child);
    ::close(pair[1]); // the child owns its half now

    // The guest connection stands before the child ever speaks: the
    // socketpair is live, and the client object owns it from here.
    client_ = server.adopt_client(pair[0]);
    if (client_ == nullptr) {
        // The refusal closed the desk's half already; the child gets the
        // bridge's goodbye and the wait that reaps it.
        static_cast<void>(::kill(child_, SIGTERM));
        while (::waitpid(child_, nullptr, 0) < 0 && errno == EINTR) {
        }
        child_ = -1;
        return false;
    }
    notice_ = new (std::nothrow) DestroyNotice{.link{}, .owner = this};
    if (notice_ == nullptr) {
        shutdown_child();
        return false;
    }
    notice_->link.notify = &GuestSpawn::client_gone;
    wl_client_add_destroy_listener(client_, &notice_->link);
    return true;
}

} // namespace vesper::compositor
// NOLINTEND(misc-include-cleaner)
