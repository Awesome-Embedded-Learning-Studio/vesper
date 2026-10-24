#include "compositor/xwayland.hpp"

#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <span>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

// xcb's C macros and typedef-aliases are the wire's own tongue; the same
// exemption the Xlib example draws.
#include <xcb/composite.h>
#include <xcb/xcb.h>
#include <xcb/xcbext.h>

#include <wayland-server-core.h>

// xcb speaks in C macros and typedef-aliases the house checks cannot see
// through; the wire's own tongue is exempt from the house spelling rules,
// from here to the file's end.
// NOLINTBEGIN(misc-include-cleaner,bugprone-signed-bitwise)

#include "compositor/server.hpp"
#include "compositor/shell.hpp"
#include "compositor/surface.hpp"

#include "visor/bindings/spawn.hpp"

namespace vesper::compositor {
namespace {

// How long the X handshake is worth waiting for: a healthy Xwayland
// answers inside a blink, a stuck one owes the desk a skip, not a hang.
constexpr int kDisplayBudgetMs = 3000;

// The highest display number the bridge will try: Xwayland instances come
// and go, but a desk that probes past :32 is fighting the whole machine.
constexpr int kDisplayCeiling = 32;

/// One abstract-namespace X listener: the socket an X client dials for
/// DISPLAY=:N, without any file under /tmp/.X11-unix. WSLg mounts that
/// directory read-only over the desk's head (it carries the system X's own
/// socket), so the file lane is closed here and the bridge builds its door
/// in the abstract lane instead, where a name is bytes and no mount cares.
/// Returns the fd with the number it landed on, or -1 with the number 0.
int open_abstract_listener(int& number) noexcept {
    for (int candidate = 1; candidate <= kDisplayCeiling; ++candidate) {
        const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) {
            return -1;
        }
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        addr.sun_path[0] = '\0'; // the abstract lane's own marker
        static_cast<void>(std::snprintf(addr.sun_path + 1, sizeof(addr.sun_path) - 1,
                                        "/tmp/.X11-unix/X%d", candidate));
        const auto length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 +
                                                   std::strlen(addr.sun_path + 1));
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), length) != 0) {
            const int error = errno;
            ::close(fd);
            if (error == EADDRINUSE) {
                continue; // somebody else's display; try the next number
            }
            return -1;
        }
        if (::listen(fd, 8) != 0) {
            ::close(fd);
            return -1;
        }
        number = candidate;
        return fd;
    }
    return -1;
}

/// One X window's face on the desk.
struct Face {
    ShellSurface* shell = nullptr;
    std::uint32_t window = 0;
    bool speaks_delete = false;
    std::uint32_t surface_id = 0;
    unsigned int protocols_reply = 0;
    unsigned int title_reply = 0;
    std::array<char, 64> title{};
};

} // namespace

struct XWayland::Impl {
    Server* server = nullptr;
    wl_client* wayland_client = nullptr; // Xwayland's own guest connection
    wl_listener client_destroy{};
    pid_t child = -1;
    xcb_connection_t* xcb = nullptr;
    xcb_window_t wm_window = 0;
    std::array<Face, kFaceCapacity> faces{};
    // The X atoms the bridge speaks, interned once.
    std::uint32_t wl_surface_id = 0;
    std::uint32_t wm_protocols = 0;
    std::uint32_t wm_delete_window = 0;
    std::uint32_t wm_s0 = 0;
    std::array<char, 8> display{};
    bool launched = false;

    static void client_gone(wl_listener* listener, void* /*data*/) noexcept {
        Impl* impl = wl_container_of(listener, impl, client_destroy);
        impl->wayland_client = nullptr;
        wl_list_remove(&listener->link);
    }
};

XWayland::XWayland() noexcept : impl_{new (std::nothrow) Impl} {}

XWayland::~XWayland() {
    if (impl_ == nullptr) {
        return;
    }
    shutdown_child();
    delete impl_;
}

int XWayland::event_fd() const noexcept {
    return impl_ != nullptr && impl_->xcb != nullptr ? xcb_get_file_descriptor(impl_->xcb) : -1;
}

void XWayland::pump() noexcept {
    if (impl_ == nullptr || impl_->xcb == nullptr) {
        return;
    }
    for (xcb_generic_event_t* event = xcb_poll_for_event(impl_->xcb); event != nullptr;
         event = xcb_poll_for_event(impl_->xcb)) {
        const std::uint8_t kind = event->response_type & 0x7f;
        if (kind == XCB_MAP_REQUEST) {
            const auto* map = reinterpret_cast<const xcb_map_request_event_t*>(event);
            static_cast<void>(xcb_map_window(impl_->xcb, map->window));
        } else if (kind == XCB_CONFIGURE_REQUEST) {
            const auto* configure = reinterpret_cast<const xcb_configure_request_event_t*>(event);
            const std::array<std::uint32_t, 7> values{
                static_cast<std::uint32_t>(configure->x),
                static_cast<std::uint32_t>(configure->y),
                configure->width,
                configure->height,
                configure->border_width,
                configure->sibling,
                configure->stack_mode,
            };
            std::array<std::uint32_t, 7> selected{};
            std::size_t count = 0;
            for (std::size_t i = 0; i < values.size(); ++i) {
                if ((configure->value_mask & (1U << i)) != 0) {
                    selected[count++] = values[i];
                }
            }
            static_cast<void>(xcb_configure_window(impl_->xcb, configure->window,
                                                   configure->value_mask, selected.data()));
        } else if (kind == XCB_CLIENT_MESSAGE) {
            const auto* message = reinterpret_cast<const xcb_client_message_event_t*>(event);
            if (message->type == impl_->wl_surface_id && message->format == 32) {
                adopt_face(message->window, message->data.data32[0]);
            }
        } else if (kind == XCB_UNMAP_NOTIFY) {
            const auto* gone = reinterpret_cast<const xcb_unmap_notify_event_t*>(event);
            drop_face(gone->window);
        } else if (kind == XCB_DESTROY_NOTIFY) {
            const auto* gone = reinterpret_cast<const xcb_destroy_notify_event_t*>(event);
            drop_face(gone->window);
        }
        std::free(event);
    }
    // Property replies and Wayland surface creation travel independently.
    // Retry without blocking either protocol while the other catches up.
    for (Face& face : impl_->faces) {
        if (face.window == 0) {
            continue;
        }
        if (face.shell != nullptr && face.shell->surface() == nullptr) {
            drop_face(face.window);
            continue;
        }
        for (unsigned int* sequence : {&face.protocols_reply, &face.title_reply}) {
            if (*sequence == 0) {
                continue;
            }
            void* raw = nullptr;
            xcb_generic_error_t* error = nullptr;
            if (xcb_poll_for_reply(impl_->xcb, *sequence, &raw, &error) == 0) {
                continue;
            }
            const auto* reply = static_cast<const xcb_get_property_reply_t*>(raw);
            if (reply != nullptr && sequence == &face.protocols_reply && reply->format == 32 &&
                reply->type == XCB_ATOM_ATOM) {
                const auto* atoms = static_cast<const xcb_atom_t*>(xcb_get_property_value(reply));
                const int count =
                    xcb_get_property_value_length(reply) / static_cast<int>(sizeof(xcb_atom_t));
                for (int i = 0; i < count; ++i) {
                    face.speaks_delete |= atoms[i] == impl_->wm_delete_window;
                }
            } else if (reply != nullptr && sequence == &face.title_reply && reply->format == 8) {
                const auto size =
                    std::min(face.title.size() - 1,
                             static_cast<std::size_t>(xcb_get_property_value_length(reply)));
                std::memcpy(face.title.data(), xcb_get_property_value(reply), size);
            }
            std::free(raw);
            std::free(error);
            *sequence = 0;
        }
        if (face.shell == nullptr && face.protocols_reply == 0 && face.title_reply == 0) {
            face.shell = impl_->server->adopt_xwindow(impl_->wayland_client, face.surface_id,
                                                      face.title.data());
        }
    }
    static_cast<void>(xcb_flush(impl_->xcb));
}

bool XWayland::alive() const noexcept {
    if (impl_ == nullptr || impl_->child <= 0) {
        return false;
    }
    const pid_t result = ::waitpid(impl_->child, nullptr, WNOHANG);
    if (result == impl_->child || (result < 0 && errno == ECHILD)) {
        impl_->child = -1;
        return false;
    }
    return result == 0 || (result < 0 && errno == EINTR);
}

const char* XWayland::display() const noexcept {
    return impl_ != nullptr ? impl_->display.data() : "";
}

bool XWayland::owns(const ShellSurface* shell) const noexcept {
    if (impl_ == nullptr || shell == nullptr) {
        return false;
    }
    return std::ranges::any_of(impl_->faces,
                               [shell](const Face& face) { return face.shell == shell; });
}

void XWayland::request_close(ShellSurface& shell) noexcept {
    if (impl_ == nullptr) {
        return;
    }
    for (const Face& face : impl_->faces) {
        if (face.shell != &shell) {
            continue;
        }
        if (face.speaks_delete) {
            // The graceful goodbye: one ClientMessage, the client's own
            // decision how to answer.
            xcb_client_message_event_t message{};
            message.response_type = XCB_CLIENT_MESSAGE;
            message.window = face.window;
            message.type = impl_->wm_protocols;
            message.format = 32;
            message.data.data32[0] = impl_->wm_delete_window;
            message.data.data32[1] = XCB_CURRENT_TIME;
            static_cast<void>(xcb_send_event(impl_->xcb, 0, face.window, XCB_EVENT_MASK_NO_EVENT,
                                             reinterpret_cast<const char*>(&message)));
        } else {
            static_cast<void>(xcb_kill_client(impl_->xcb, face.window));
        }
        static_cast<void>(xcb_flush(impl_->xcb));
        return;
    }
}

} // namespace vesper::compositor

// The launch and the X bookkeeping, the bridge's private hands in full.
namespace vesper::compositor {

void XWayland::adopt_face(std::uint32_t window, std::uint32_t surface_id) noexcept {
    drop_face(window);
    for (Face& face : impl_->faces) {
        if (face.window == 0) {
            face.window = window;
            face.surface_id = surface_id;
            face.protocols_reply =
                xcb_get_property(impl_->xcb, 0, window, impl_->wm_protocols, XCB_ATOM_ATOM, 0, 64)
                    .sequence;
            face.title_reply = xcb_get_property(impl_->xcb, 0, window, XCB_ATOM_WM_NAME,
                                                XCB_GET_PROPERTY_TYPE_ANY, 0, 16)
                                   .sequence;
            return;
        }
    }
}

void XWayland::drop_face(std::uint32_t window) noexcept {
    for (Face& face : impl_->faces) {
        if (face.window == window) {
            if (face.protocols_reply != 0) {
                xcb_discard_reply(impl_->xcb, face.protocols_reply);
            }
            if (face.title_reply != 0) {
                xcb_discard_reply(impl_->xcb, face.title_reply);
            }
            delete face.shell;
            face = Face{};
            return;
        }
    }
}

bool XWayland::launch(Server& server, const visor::host::HostTable& table) noexcept {
    if (impl_ == nullptr || impl_->launched || !server.valid()) {
        return false;
    }
    // A table without the fourth slot cannot birth: the fake-host stance,
    // refused up front instead of deep inside the wiring.
    if (!table.can_spawn()) {
        return false;
    }
    impl_->server = &server;

    // The binary: the environment's word first, the usual seat second.
    const char* binary = std::getenv("VESPER_XWAYLAND");
    if (binary == nullptr || binary[0] == '\0') {
        binary = "/usr/bin/Xwayland";
    }
    if (::access(binary, X_OK) != 0) {
        return false;
    }

    int listen_fd = -1;
    int display_number = 0;
    std::array<int, 2> ready_pair{-1, -1};
    std::array<int, 2> wayland_pair{-1, -1};
    std::array<int, 2> wm_pair{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, wayland_pair.data()) != 0 ||
        ::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, wm_pair.data()) != 0 ||
        ::pipe2(ready_pair.data(), O_CLOEXEC) != 0) {
        for (const int fd : ready_pair) {
            ::close(fd);
        }
        for (const int fd : wayland_pair) {
            ::close(fd);
        }
        for (const int fd : wm_pair) {
            ::close(fd);
        }
        return false;
    }
    listen_fd = open_abstract_listener(display_number);
    if (listen_fd < 0) {
        for (const int fd : ready_pair) {
            ::close(fd);
        }
        for (const int fd : wayland_pair) {
            ::close(fd);
        }
        for (const int fd : wm_pair) {
            ::close(fd);
        }
        return false;
    }

    // The child's legs: WAYLAND_SOCKET carries the guest connection, the
    // wm and listen words carry their own descriptors, and the display
    // number is ours -- picked when the abstract listener found its slot.
    std::array<char, 16> ready_word{};
    static_cast<void>(std::snprintf(ready_word.data(), ready_word.size(), "%d", ready_pair[1]));
    std::array<char, 16> wm_word{};
    std::array<char, 16> listen_word{};
    std::array<char, 16> display_word{};
    static_cast<void>(std::snprintf(wm_word.data(), wm_word.size(), "%d", wm_pair[1]));
    static_cast<void>(std::snprintf(listen_word.data(), listen_word.size(), "%d", listen_fd));
    static_cast<void>(
        std::snprintf(display_word.data(), display_word.size(), ":%d", display_number));
    std::array<char, 16> wayland_word{};
    static_cast<void>(
        std::snprintf(wayland_word.data(), wayland_word.size(), "%d", wayland_pair[1]));

    // The child's environment: the desk's own words plus the one word that
    // names the guest connection. A stale WAYLAND_SOCKET from the outer
    // world is skipped so the fresh word is the only one the child sees,
    // the same overwrite setenv once played inside the fork.
    std::array<char, 24> socket_word{};
    static_cast<void>(std::snprintf(socket_word.data(), socket_word.size(), "WAYLAND_SOCKET=%d",
                                    wayland_pair[1]));
    // Kept in step with the POSIX filling's env budget: a desk environment
    // fatter than this disarms the bridge rather than silently slimming
    // the child's world.
    constexpr std::size_t env_word_budget = 256;
    std::array<const char*, env_word_budget + 1> env_words{};
    std::size_t env_count = 0;
    bool env_over_budget = false;
    for (char* const* word = ::environ; *word != nullptr; ++word) {
        if (std::strncmp(*word, "WAYLAND_SOCKET=", 15) == 0) {
            continue;
        }
        if (env_count == env_word_budget) {
            env_over_budget = true;
            break;
        }
        env_words[env_count] = *word;
        ++env_count;
    }
    if (env_over_budget) {
        ::close(listen_fd);
        for (const int fd : ready_pair) {
            ::close(fd);
        }
        for (const int fd : wayland_pair) {
            ::close(fd);
        }
        for (const int fd : wm_pair) {
            ::close(fd);
        }
        return false;
    }
    env_words[env_count] = socket_word.data();

    // The fourth slot does the birth: the four bridge channels ride at
    // their own numbers (the pass-as-is shape), the whitelist stance closes
    // everything else above stdio, and the explicit listen fd is the whole
    // socket story -- Xwayland builds none of its own, which is what keeps
    // it clear of the read-only /tmp/.X11-unix. The display number in the
    // argument is the slot the abstract listener already holds.
    const std::array<visor::host::FdGift, 4> gifts{
        visor::host::FdGift{.child_fd = wayland_pair[1], .parent_fd = wayland_pair[1]},
        visor::host::FdGift{.child_fd = wm_pair[1], .parent_fd = wm_pair[1]},
        visor::host::FdGift{.child_fd = listen_fd, .parent_fd = listen_fd},
        visor::host::FdGift{.child_fd = ready_pair[1], .parent_fd = ready_pair[1]},
    };
    const std::array<const char*, 11> argv{
        "Xwayland", display_word.data(), "-rootless",  "-listenfd",       listen_word.data(),
        "-wm",      wm_word.data(),      "-displayfd", ready_word.data(), "-terminate",
        "3",
    };
    const visor::host::SpawnRequest request{
        .path = binary,
        .argv = argv,
        .env = std::span<const char* const>(env_words.data(), env_count + 1),
        .gifts = gifts,
        .close_other_fds = true,
    };
    const int child = table.spawn_child(request);
    if (child < 0) {
        ::close(listen_fd);
        for (const int fd : ready_pair) {
            ::close(fd);
        }
        for (const int fd : wayland_pair) {
            ::close(fd);
        }
        for (const int fd : wm_pair) {
            ::close(fd);
        }
        return false;
    }
    impl_->child = static_cast<pid_t>(child);
    ::close(ready_pair[1]);
    ::close(wm_pair[1]);
    ::close(listen_fd); // the child owns the listener now
    ::close(wayland_pair[1]);

    // The guest connection stands before the child ever speaks: the socket
    // is live, the client object owns it from here.
    impl_->wayland_client = server.adopt_client(wayland_pair[0]);
    if (impl_->wayland_client == nullptr) {
        ::close(ready_pair[0]);
        ::close(wm_pair[0]);
        shutdown_child();
        return false;
    }
    impl_->client_destroy.notify = &Impl::client_gone;
    wl_client_add_destroy_listener(impl_->wayland_client, &impl_->client_destroy);

    // Xwayland must finish its Wayland roundtrips before the X setup can
    // answer. Keep serving and flushing until its displayfd says ready.
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds{kDisplayBudgetMs};
    std::array<char, 16> ready_bytes{};
    std::size_t received = 0;
    bool ready = false;
    while (std::chrono::steady_clock::now() < deadline && alive()) {
        if (!server.dispatch(0)) {
            break;
        }
        server.flush_clients();
        std::array<pollfd, 2> waiters{
            pollfd{.fd = server.event_fd(), .events = POLLIN, .revents = 0},
            pollfd{.fd = ready_pair[0], .events = POLLIN, .revents = 0},
        };
        if (::poll(waiters.data(), waiters.size(), 10) < 0 && errno != EINTR) {
            break;
        }
        if (waiters[1].revents != 0) {
            const ssize_t count = ::read(ready_pair[0], ready_bytes.data() + received,
                                         ready_bytes.size() - 1 - received);
            if (count <= 0) {
                break;
            }
            received += static_cast<std::size_t>(count);
            if (ready_bytes[received - 1] == '\n') {
                std::array<char, 16> expected{};
                static_cast<void>(
                    std::snprintf(expected.data(), expected.size(), "%d\n", display_number));
                ready = std::strcmp(ready_bytes.data(), expected.data()) == 0;
                break;
            }
            if (received == ready_bytes.size() - 1) {
                break;
            }
        }
    }
    ::close(ready_pair[0]);
    if (!ready) {
        ::close(wm_pair[0]);
        shutdown_child();
        return false;
    }
    impl_->xcb = xcb_connect_to_fd(wm_pair[0], nullptr);
    if (impl_->xcb == nullptr || xcb_connection_has_error(impl_->xcb) != 0) {
        shutdown_child();
        return false;
    }

    const xcb_setup_t* setup = xcb_get_setup(impl_->xcb);
    const xcb_screen_iterator_t screens = xcb_setup_roots_iterator(setup);
    if (screens.rem == 0) {
        shutdown_child();
        return false;
    }
    const xcb_screen_t* screen = screens.data;

    // The manager's identity: one window of our own, the WM_S0 selection
    // under it, and the root's structural words flowing our way.
    impl_->wm_window = xcb_generate_id(impl_->xcb);
    static_cast<void>(xcb_create_window(impl_->xcb, XCB_COPY_FROM_PARENT, impl_->wm_window,
                                        screen->root, -1, -1, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY,
                                        XCB_COPY_FROM_PARENT, 0, nullptr));
    impl_->wl_surface_id = intern("WL_SURFACE_ID");
    impl_->wm_protocols = intern("WM_PROTOCOLS");
    impl_->wm_delete_window = intern("WM_DELETE_WINDOW");
    impl_->wm_s0 = intern("WM_S0");
    static_cast<void>(
        xcb_set_selection_owner(impl_->xcb, impl_->wm_window, impl_->wm_s0, XCB_CURRENT_TIME));
    static_cast<void>(xcb_change_window_attributes(
        impl_->xcb, screen->root, XCB_CW_EVENT_MASK,
        std::array<std::uint32_t, 1>{
            XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
        }
            .data()));
    xcb_discard_reply(impl_->xcb, xcb_composite_query_version(impl_->xcb, 0, 4).sequence);
    static_cast<void>(
        xcb_composite_redirect_subwindows(impl_->xcb, screen->root, XCB_COMPOSITE_REDIRECT_MANUAL));
    static_cast<void>(xcb_flush(impl_->xcb));

    static_cast<void>(
        std::snprintf(impl_->display.data(), impl_->display.size(), "%s", display_word.data()));
    impl_->launched = true;
    return true;
}

void XWayland::shutdown_child() noexcept {
    for (const Face& face : impl_->faces) {
        if (face.window != 0) {
            drop_face(face.window);
        }
    }
    if (impl_->xcb != nullptr) {
        xcb_disconnect(impl_->xcb);
        impl_->xcb = nullptr;
    }
    if (impl_->child > 0) {
        static_cast<void>(::kill(impl_->child, SIGTERM));
        for (int waited = 0; waited < 50 && alive(); ++waited) {
            static_cast<void>(::usleep(10'000));
        }
        if (impl_->child > 0) {
            static_cast<void>(::kill(impl_->child, SIGKILL));
            while (::waitpid(impl_->child, nullptr, 0) < 0 && errno == EINTR) {
            }
            impl_->child = -1;
        }
    }
    if (impl_->wayland_client != nullptr) {
        wl_client_destroy(impl_->wayland_client);
    }
}

std::uint32_t XWayland::intern(const char* name) noexcept {
    const xcb_intern_atom_cookie_t cookie =
        xcb_intern_atom(impl_->xcb, 0, static_cast<std::uint16_t>(std::strlen(name)), name);
    xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(impl_->xcb, cookie, nullptr);
    const std::uint32_t atom = reply != nullptr ? reply->atom : XCB_NONE;
    std::free(reply);
    return atom;
}

} // namespace vesper::compositor
// NOLINTEND(misc-include-cleaner,bugprone-signed-bitwise)
