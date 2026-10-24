#include "compositor/server.hpp"

#include "compositor/seat.hpp"
#include "compositor/shell.hpp"
#include "compositor/shm.hpp"
#include "compositor/surface.hpp"

#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <new>

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>

#include "xdg-shell-server-protocol.h"

namespace vesper::compositor {
namespace {

// The versions the four globals advertise, the caps the heart speaks. The
// compositor takes 4 for damage_buffer, the buffer-relative damage word the
// frame chapter wants. shm has been one version its whole life. The seat
// takes 5, the floor under the pointer's frame words the input chapter (C2)
// plans to speak -- the heart itself binds no capabilities, the seat is a
// named empty chair. xdg_wm_base stays at 1, the version every guest in the
// ecosystem still accepts.
constexpr int kCompositorVersion = 4;
constexpr int kShmVersion = 1;
constexpr int kSeatVersion = 5;
constexpr int kWmBaseVersion = 1;
constexpr int kOutputVersion = 2;

// The client tally, shared with the per-client tickets below: a private
// nested Impl cannot be named from file-scope helpers, and the count is all
// those helpers need anyway.
struct Tally {
    int clients = 0;
};

// One adopted client's ticket: the destroy signal fires when the guest
// hangs up or errors out, and the tally it holds goes down with it.
struct ClientTicket {
    wl_listener destroy{};
    Tally* tally = nullptr;
};

/// The extent the desk reported for its own canvas, read at output bind
/// time. A file-scope shape because the private Impl cannot be named here.
struct OutputExtent {
    int w = 1280;
    int h = 800;
};

/// The one output every guest hears about: the extent the desk reported,
/// spoken once at bind time and never revised. Xwayland needs an output
/// before its X screen stands up, which makes this the smallest word the
/// host must say for the bridge to arm.
// The libwayland bind callback fixes this parameter as void*.
// NOLINTNEXTLINE(misc-const-correctness)
void bind_output(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) noexcept {
    const auto* extent = static_cast<const OutputExtent*>(data);
    wl_resource* resource = wl_resource_create(client, &wl_output_interface,
                                               static_cast<int>(version < 2U ? version : 2U), id);
    if (resource == nullptr) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_post_event(resource, WL_OUTPUT_GEOMETRY, 0, 0, 320, 240, 0, "vesper", "vesper", 0);
    wl_resource_post_event(resource, WL_OUTPUT_MODE,
                           WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED, extent->w, extent->h,
                           60000);
    if (version >= 2U) {
        wl_resource_post_event(resource, WL_OUTPUT_SCALE, 1);
        wl_resource_post_event(resource, WL_OUTPUT_DONE);
    }
}

void client_gone(wl_listener* listener, void* /*data*/) noexcept {
    // The macro wants a declared pointer to read the type from, the same
    // hand the C compositors play; auto would read itself.
    const ClientTicket* ticket = nullptr;
    ticket = wl_container_of(listener, ticket, destroy);
    --ticket->tally->clients;
    delete ticket;
}

/// Creates a resource at the guest's requested version (libwayland itself
/// refuses a bind above the advertised one) and hands it an empty table.
/// This is the bind door every inert global shares.
[[nodiscard]] wl_resource* inert_resource(wl_client* client, std::uint32_t version,
                                          std::uint32_t id,
                                          const wl_interface* interface) noexcept {
    wl_resource* resource = wl_resource_create(client, interface, static_cast<int>(version), id);
    if (resource == nullptr) {
        wl_client_post_no_memory(client);
    }
    return resource;
}

} // namespace

// The server's own state, out of the anonymous namespace: a nested-class
// definition must sit in an enclosing namespace, not a nested one.
struct Server::Impl {
    wl_display* display = nullptr;
    wl_event_loop* loop = nullptr;
    wl_global* compositor = nullptr;
    wl_global* shm = nullptr;
    wl_global* seat_global = nullptr;
    wl_global* wm_base = nullptr;
    wl_global* output = nullptr;
    Tally tally{};
    SurfaceBook surfaces{};
    ShellBook shells{};
    OutputExtent output_extent{};
    Seat seat;
    std::uint32_t frame_ms = 0;
};

Server::Server() noexcept {
    impl_ = new (std::nothrow) Impl;
    if (impl_ == nullptr) {
        return;
    }
    impl_->display = wl_display_create();
    if (impl_->display == nullptr) {
        delete impl_;
        impl_ = nullptr;
        return;
    }
    impl_->loop = wl_display_get_event_loop(impl_->display);
    impl_->compositor = wl_global_create(impl_->display, &wl_compositor_interface,
                                         kCompositorVersion, &impl_->surfaces, &bind_compositor);
    impl_->shm =
        wl_global_create(impl_->display, &wl_shm_interface, kShmVersion, nullptr, &bind_shm);
    impl_->seat_global = wl_global_create(impl_->display, &wl_seat_interface, kSeatVersion,
                                          &impl_->seat, &bind_seat);
    impl_->wm_base = wl_global_create(impl_->display, &xdg_wm_base_interface, kWmBaseVersion,
                                      &impl_->shells, &bind_wm_base);
    impl_->output = wl_global_create(impl_->display, &wl_output_interface, kOutputVersion,
                                     &impl_->output_extent, &bind_output);
    if (impl_->compositor == nullptr || impl_->shm == nullptr || impl_->seat_global == nullptr ||
        impl_->wm_base == nullptr || impl_->output == nullptr) {
        // The globals own nothing yet, so refusing the whole display is the
        // only honest cleanup a half-built heart has.
        wl_display_destroy(impl_->display);
        delete impl_;
        impl_ = nullptr;
    }
}

Server::~Server() {
    if (impl_ == nullptr) {
        return;
    }
    // libwayland keeps clients outside the display's own cleanup on purpose
    // (a compositor's client state is the compositor's to close), so the
    // goodbye order is the documented one: drop the guests first -- every
    // ticket fires, the tally settles to zero -- then the display itself.
    wl_display_destroy_clients(impl_->display);
    wl_display_destroy(impl_->display);
    delete impl_;
}

bool Server::valid() const noexcept {
    return impl_ != nullptr;
}

int Server::event_fd() const noexcept {
    return impl_ != nullptr ? wl_event_loop_get_fd(impl_->loop) : -1;
}

void Server::note_output_extent(int width, int height) noexcept {
    if (impl_ != nullptr) {
        impl_->output_extent.w = width;
        impl_->output_extent.h = height;
    }
}

bool Server::dispatch(int timeout_ms) noexcept {
    if (impl_ == nullptr) {
        return false;
    }
    return wl_event_loop_dispatch(impl_->loop, timeout_ms) >= 0;
}

void Server::flush_clients() noexcept {
    if (impl_ != nullptr) {
        wl_display_flush_clients(impl_->display);
    }
}

void Server::end_frame() noexcept {
    if (impl_ == nullptr) {
        return;
    }
    // The desk presents once per round, and this is that moment: the
    // callbacks collected since the last frame hear their done words, the
    // consumed buffers go home, and everything leaves in one flush. The
    // cadence is the desk's own -- settle rounds in between flush words
    // but never fire debts.
    for (std::size_t i = 0; i < impl_->surfaces.count(); ++i) {
        impl_->surfaces.at(i)->end_frame(impl_->frame_ms);
    }
    wl_display_flush_clients(impl_->display);
}

int Server::surface_count() const noexcept {
    return impl_ != nullptr ? static_cast<int>(impl_->surfaces.count()) : 0;
}

ShellSurface* Server::adopt_xwindow(wl_client* client, std::uint32_t surface_id,
                                    const char* title) noexcept {
    if (impl_ == nullptr) {
        return nullptr;
    }
    for (std::size_t i = 0; i < impl_->surfaces.count(); ++i) {
        Surface* surface = impl_->surfaces.at(i);
        if (wl_resource_get_client(surface->resource()) == client &&
            wl_resource_get_id(surface->resource()) == surface_id) {
            return ShellSurface::adopt_xwindow(*surface, impl_->shells, title);
        }
    }
    return nullptr;
}

Surface::Frame Server::surface_frame(int index) const noexcept {
    if (impl_ == nullptr) {
        return Surface::Frame{};
    }
    const Surface* surface = impl_->surfaces.at(static_cast<std::size_t>(index));
    return surface != nullptr ? surface->frame() : Surface::Frame{};
}

int Server::shell_surface_count() const noexcept {
    return impl_ != nullptr ? static_cast<int>(impl_->shells.count()) : 0;
}

ShellSurface* Server::shell_surface(int index) const noexcept {
    if (impl_ == nullptr) {
        return nullptr;
    }
    return impl_->shells.at(static_cast<std::size_t>(index));
}

void Server::set_shell_hooks(const ShellHooks& hooks) noexcept {
    if (impl_ != nullptr) {
        impl_->shells.hooks = hooks;
    }
}

Seat& Server::seat() noexcept {
    static Seat none; // an invalid server's honest empty chair
    return impl_ != nullptr ? impl_->seat : none;
}

void Server::note_time_ms(std::uint32_t ms) noexcept {
    if (impl_ != nullptr) {
        impl_->seat.note_time_ms(ms);
        impl_->frame_ms = ms;
    }
}

void Server::set_frame_time_ms(std::uint32_t ms) noexcept {
    if (impl_ != nullptr) {
        impl_->frame_ms = ms;
    }
}

wl_client* Server::adopt_client(int fd) noexcept {
    if (impl_ == nullptr) {
        ::close(fd);
        return nullptr;
    }
    wl_client* client = wl_client_create(impl_->display, fd);
    if (client == nullptr) {
        ::close(fd);
        return nullptr;
    }
    auto* ticket = new (std::nothrow) ClientTicket;
    if (ticket == nullptr) {
        wl_client_destroy(client); // destroys the client and closes its fd
        return nullptr;
    }
    ticket->tally = &impl_->tally;
    ticket->destroy.notify = &client_gone;
    wl_client_add_destroy_listener(client, &ticket->destroy);
    ++impl_->tally.clients;
    return client;
}

const char* Server::publish_socket() noexcept {
    if (impl_ == nullptr) {
        return nullptr;
    }
    return wl_display_add_socket_auto(impl_->display);
}

int Server::client_count() const noexcept {
    return impl_ != nullptr ? impl_->tally.clients : 0;
}

} // namespace vesper::compositor
