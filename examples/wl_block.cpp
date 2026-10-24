// The compositor lane's living guest: a small Wayland client that draws
// one amber square bouncing on ink, frame by frame, on whatever desk
// carries it. Every frame rides the whole C-series pipeline -- shm buffer,
// damage word, frame callback on the desk's own beat. Run it beside the
// compositor example:
//   WAYLAND_DISPLAY=<the socket it printed> wl_block
// The desk's close box is the goodbye.
#include <sys/mman.h>
#include <unistd.h>

#include <poll.h>

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-util.h>

#include "xdg-shell-client-protocol.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <print>

namespace {

constexpr int kWidth = 160;
constexpr int kHeight = 120;
constexpr int kStrideWords = kWidth;
constexpr std::size_t kPoolBytes = static_cast<std::size_t>(kStrideWords) * kHeight * 4U * 2U;

constexpr std::uint32_t kInk = 0xff2b'2620U;   // the desk's soft ink, dark
constexpr std::uint32_t kAmber = 0xffc9'8a2dU; // the desk's dusk gold
constexpr int kSquare = 28;

// NOLINTBEGIN(misc-include-cleaner): the C client headers name their own shapes.
struct Guest {
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    xdg_wm_base* wm_base = nullptr;
    wl_surface* surface = nullptr;
    struct xdg_surface* role_surface = nullptr;
    xdg_toplevel* toplevel = nullptr;
    std::array<wl_buffer*, 2> buffers{};
    std::uint32_t* words = nullptr;
    int active = 0;
    int x = 24;
    int y = 18;
    int dx = 3;
    int dy = 2;
    bool configured = false;
    bool close_requested = false;
    int beats = 0;
};

void registry_global(void* data, wl_registry* registry, std::uint32_t name, const char* interface,
                     std::uint32_t version) noexcept {
    auto* guest = static_cast<Guest*>(data);
    const std::uint32_t bound = version < 1U ? version : 1U;
    if (std::strcmp(interface, "wl_compositor") == 0) {
        guest->compositor = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, bound));
    } else if (std::strcmp(interface, "wl_shm") == 0) {
        guest->shm =
            static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, bound));
    } else if (std::strcmp(interface, "xdg_wm_base") == 0) {
        guest->wm_base = static_cast<xdg_wm_base*>(
            wl_registry_bind(registry, name, &xdg_wm_base_interface, bound));
    }
}

void registry_global_remove(void* /*data*/, wl_registry* /*registry*/,
                            std::uint32_t /*name*/) noexcept {}

const wl_registry_listener kRegistry = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

void wm_base_ping(void* /*data*/, xdg_wm_base* wm_base, std::uint32_t serial) noexcept {
    xdg_wm_base_pong(wm_base, serial);
}

const xdg_wm_base_listener kWmBase = {
    .ping = wm_base_ping,
};

void xdg_configure(void* data, xdg_surface* xdg_surface, std::uint32_t serial) noexcept {
    auto* guest = static_cast<Guest*>(data);
    guest->configured = true;
    xdg_surface_ack_configure(xdg_surface, serial);
}

const xdg_surface_listener kXdgSurface = {
    .configure = xdg_configure,
};

void toplevel_close(void* data, xdg_toplevel* /*toplevel*/) noexcept {
    static_cast<Guest*>(data)->close_requested = true;
}

void toplevel_configure(void* /*data*/, xdg_toplevel* /*toplevel*/, std::int32_t /*width*/,
                        std::int32_t /*height*/, wl_array* /*states*/) noexcept {}

const xdg_toplevel_listener kToplevel = {
    .configure = toplevel_configure,
    .close = toplevel_close,
    .configure_bounds = nullptr, // sizing advice the guest never follows
    .wm_capabilities = nullptr,  // what the desk allows; the guest takes it
};

void frame_done(void* data, wl_callback* callback, std::uint32_t /*callback_data*/) noexcept {
    // The beat's receipt: destroy the proxy (its one job is done) and count
    // the beat the square steps on.
    wl_callback_destroy(callback);
    ++static_cast<Guest*>(data)->beats;
}

const wl_callback_listener kFrame = {
    .done = frame_done,
};
// NOLINTEND(misc-include-cleaner)

/// Paints one frame: ink ground, the amber square where it stands, and the
/// two-step trail the previous frame left faint.
void paint(const Guest& guest) noexcept {
    std::uint32_t* row =
        guest.words + (static_cast<std::size_t>(guest.active) * kStrideWords * kHeight);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            row[(static_cast<std::size_t>(y) * kStrideWords) + static_cast<std::size_t>(x)] = kInk;
        }
    }
    for (int y = guest.y; y < guest.y + kSquare; ++y) {
        for (int x = guest.x; x < guest.x + kSquare; ++x) {
            if (x >= 0 && x < kWidth && y >= 0 && y < kHeight) {
                row[(static_cast<std::size_t>(y) * kStrideWords) + static_cast<std::size_t>(x)] =
                    kAmber;
            }
        }
    }
}

} // namespace

int main() {
    Guest guest;
    wl_display* display = wl_display_connect(nullptr);
    if (display == nullptr) {
        std::println(stderr, "wl_block: no display (set WAYLAND_DISPLAY to the desk's socket)");
        return 1;
    }
    wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &kRegistry, &guest);
    static_cast<void>(wl_display_roundtrip(display));
    wl_registry_destroy(registry);
    if (guest.compositor == nullptr || guest.shm == nullptr || guest.wm_base == nullptr) {
        std::println(stderr, "wl_block: the desk did not offer the three globals a block needs");
        return 1;
    }
    xdg_wm_base_add_listener(guest.wm_base, &kWmBase, &guest);

    // The pool: one memfd, two buffers, the classic pair.
    const int fd = ::memfd_create("wl-block", 0);
    if (fd < 0 || ::ftruncate(fd, static_cast<off_t>(kPoolBytes)) != 0) {
        std::println(stderr, "wl_block: no pool");
        return 1;
    }
    void* map = ::mmap(nullptr, kPoolBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        std::println(stderr, "wl_block: no mapping");
        return 1;
    }
    guest.words = static_cast<std::uint32_t*>(map);
    wl_shm_pool* pool = wl_shm_create_pool(guest.shm, fd, static_cast<std::int32_t>(kPoolBytes));
    ::close(fd);
    for (int i = 0; i < 2; ++i) {
        const auto offset =
            static_cast<std::int32_t>(static_cast<std::size_t>(i) * kStrideWords * kHeight * 4U);
        guest.buffers[i] = wl_shm_pool_create_buffer(pool, offset, kWidth, kHeight, kWidth * 4,
                                                     WL_SHM_FORMAT_XRGB8888);
    }
    wl_shm_pool_destroy(pool);

    guest.surface = wl_compositor_create_surface(guest.compositor);
    guest.role_surface = xdg_wm_base_get_xdg_surface(guest.wm_base, guest.surface);
    xdg_surface_add_listener(guest.role_surface, &kXdgSurface, &guest);
    guest.toplevel = xdg_surface_get_toplevel(guest.role_surface);
    xdg_toplevel_add_listener(guest.toplevel, &kToplevel, &guest);
    xdg_toplevel_set_title(guest.toplevel, "LIUXU");
    wl_surface_commit(guest.surface);
    while (!guest.configured && wl_display_dispatch(display) >= 0) {
    }

    // The frame loop: paint, commit, wait for the desk's beat, step. The
    // desk's cadence is the bouncer's own -- whatever beat it presents on,
    // the square dances to.
    while (!guest.close_requested) {
        const int beats_before = guest.beats;
        guest.active = 1 - guest.active;
        paint(guest);
        wl_surface_attach(guest.surface, guest.buffers[guest.active], 0, 0);
        wl_surface_damage(guest.surface, 0, 0, kWidth, kHeight);
        wl_callback* beat = wl_surface_frame(guest.surface);
        wl_callback_add_listener(beat, &kFrame, &guest);
        wl_surface_commit(guest.surface);
        static_cast<void>(wl_display_flush(display));
        bool stepped = false;
        while (!stepped && !guest.close_requested) {
            struct pollfd waiter{
                .fd = wl_display_get_fd(display),
                .events = POLLIN,
                .revents = 0,
            };
            if (::poll(&waiter, 1, 100) == 0) {
                stepped = true; // a desk that never beats still owes the square its steps
                break;
            }
            if (wl_display_dispatch(display) < 0) {
                guest.close_requested = true;
                break;
            }
            stepped = guest.beats != beats_before;
        }
        guest.x += guest.dx;
        guest.y += guest.dy;
        if (guest.x < 0 || guest.x + kSquare > kWidth) {
            guest.dx = -guest.dx;
            guest.x += guest.dx;
        }
        if (guest.y < 0 || guest.y + kSquare > kHeight) {
            guest.dy = -guest.dy;
            guest.y += guest.dy;
        }
    }
    return 0;
}
