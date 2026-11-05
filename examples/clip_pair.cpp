// The clipboard lane's two demo cars, the first real riders of the desk's
// selection channel (A1): run one window as the copy side and one as the
// paste side, each its own process, each a plain guest like wl_block.
//   WAYLAND_DISPLAY=<the desk's socket> clip_pair copy
//   WAYLAND_DISPLAY=<the desk's socket> clip_pair paste
// Click the copy window and its word rides the desk's clipboard; click the
// paste window and the word pours back through the pipe, onto its square
// and onto stdout. The keyboard follows the pointer, so the click itself
// is what carries the selection across.
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-util.h>

#include "xdg-shell-client-protocol.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <print>
#include <string_view>

namespace {

constexpr int kWidth = 160;
constexpr int kHeight = 120;
constexpr int kStrideWords = kWidth;
constexpr std::size_t kPoolBytes = static_cast<std::size_t>(kStrideWords) * kHeight * 4U * 2U;

constexpr std::uint32_t kInk = 0xff2b'2620U;    // the desk's soft ink, dark
constexpr std::uint32_t kAmber = 0xffc9'8a2dU;  // the desk's dusk gold
constexpr std::uint32_t kDimmed = 0xff4e'463eU; // the square asleep
constexpr int kSquare = 28;

// The word the copy car carries. Any bytes would do; this one names itself.
constexpr std::string_view kPayload = "vesper clipboard";

// NOLINTBEGIN(misc-include-cleaner): the C client headers name their own shapes.
struct Guest {
    bool copy_role = true;

    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    xdg_wm_base* wm_base = nullptr;
    wl_seat* seat = nullptr;
    wl_data_device_manager* manager = nullptr;

    wl_surface* surface = nullptr;
    xdg_surface* role_surface = nullptr;
    xdg_toplevel* toplevel = nullptr;
    std::array<wl_buffer*, 2> buffers{};
    std::uint32_t* words = nullptr;
    int active = 0;

    wl_pointer* pointer = nullptr;
    bool clicked = false;
    std::uint32_t click_serial = 0;

    // The clipboard hands, one set per role.
    wl_data_device* device = nullptr;
    wl_data_source* source = nullptr; // copy: the word's home
    wl_data_offer* offer = nullptr;   // paste: the latest offer heard
    bool has_selection = false;       // paste: a selection stands
    bool wants_text = false;          // paste: the offer speaks text
    bool armed = false;               // copy: the word is on the clipboard
    bool poured = false;              // paste: the word came home

    bool configured = false;
    bool close_requested = false;
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
    } else if (std::strcmp(interface, "wl_seat") == 0) {
        guest->seat =
            static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, bound));
    } else if (std::strcmp(interface, "wl_data_device_manager") == 0) {
        guest->manager = static_cast<wl_data_device_manager*>(
            wl_registry_bind(registry, name, &wl_data_device_manager_interface, bound));
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
    static_cast<Guest*>(data)->configured = true;
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

void pointer_button(void* data, wl_pointer* /*pointer*/, std::uint32_t serial,
                    std::uint32_t /*time*/, std::uint32_t button, std::uint32_t state) noexcept {
    auto* guest = static_cast<Guest*>(data);
    if (button == 0x110 && state == WL_POINTER_BUTTON_STATE_PRESSED) { // BTN_LEFT
        guest->clicked = true;
        guest->click_serial = serial;
    }
}

void pointer_enter(void* /*data*/, wl_pointer* /*pointer*/, std::uint32_t /*serial*/,
                   wl_surface* /*surface*/, std::int32_t /*sx*/, std::int32_t /*sy*/) noexcept {}

void pointer_leave(void* /*data*/, wl_pointer* /*pointer*/, std::uint32_t /*serial*/,
                   wl_surface* /*surface*/) noexcept {}

void pointer_motion(void* /*data*/, wl_pointer* /*pointer*/, std::uint32_t /*time*/,
                    std::int32_t /*sx*/, std::int32_t /*sy*/) noexcept {}

const wl_pointer_listener kPointer = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = nullptr, // the wheel's words; this car has no wheels
    .frame = nullptr,
    .axis_source = nullptr,
    .axis_stop = nullptr,
    .axis_discrete = nullptr,
    .axis_value120 = nullptr,
    .axis_relative_direction = nullptr,
    .warp = nullptr,
};

void seat_capabilities(void* data, wl_seat* seat, std::uint32_t caps) noexcept {
    auto* guest = static_cast<Guest*>(data);
    if ((caps & WL_SEAT_CAPABILITY_POINTER) != 0 && guest->pointer == nullptr) {
        guest->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(guest->pointer, &kPointer, guest);
    }
}

const wl_seat_listener kSeat = {
    .capabilities = seat_capabilities,
    .name = nullptr, // the desk's seat needs no name from the guest
};

// The copy half: the source's answers. The desk hands over a pipe end and
// the word pours down it; a cancelled source gave the clipboard away.
void source_target(void* /*data*/, wl_data_source* /*source*/, const char* /*mime_type*/) noexcept {
}

void source_send(void* /*data*/, wl_data_source* /*source*/, const char* /*mime_type*/,
                 std::int32_t fd) noexcept {
    std::size_t sent = 0;
    while (sent < kPayload.size()) {
        const auto wrote = ::write(fd, kPayload.data() + sent, kPayload.size() - sent);
        if (wrote <= 0) {
            break;
        }
        sent += static_cast<std::size_t>(wrote);
    }
    ::close(fd); // the pour's end: the reader's EOF rides on this close
}

void source_cancelled(void* data, wl_data_source* /*source*/) noexcept {
    static_cast<Guest*>(data)->armed = false;
}

const wl_data_source_listener kSource = {
    .target = source_target,
    .send = source_send,
    .cancelled = source_cancelled,
    .dnd_drop_performed = nullptr, // the drag half's words; this car does not drag
    .dnd_finished = nullptr,
    .action = nullptr,
};

// The paste half: the offer's word first -- what flavors the standing
// selection speaks -- then the device's. A fresh offer rides every
// crossing; the selection word behind it names the one that stands.
void offer_offer(void* data, wl_data_offer* /*offer*/, const char* mime_type) noexcept {
    auto* guest = static_cast<Guest*>(data);
    if (std::strcmp(mime_type, "text/plain;charset=utf-8") == 0) {
        guest->wants_text = true;
    }
}

const wl_data_offer_listener kOffer = {
    .offer = offer_offer,
    .source_actions = nullptr, // the drag half's words; this car does not drag
    .action = nullptr,
};

void device_data_offer(void* data, wl_data_device* /*device*/, wl_data_offer* id) noexcept {
    auto* guest = static_cast<Guest*>(data);
    if (guest->offer != nullptr) {
        wl_data_offer_destroy(guest->offer); // the polite goodbye to the retired one
    }
    guest->offer = id;
    guest->wants_text = false;
    wl_data_offer_add_listener(id, &kOffer, guest);
}

// NOLINTNEXTLINE(misc-const-correctness): the listener table fixes the word's type.
void device_selection(void* data, wl_data_device* /*device*/, wl_data_offer* id) noexcept {
    auto* guest = static_cast<Guest*>(data);
    guest->has_selection = id != nullptr;
    if (id == nullptr) {
        guest->wants_text = false;
    }
}

const wl_data_device_listener kDevice = {
    .data_offer = device_data_offer,
    .enter = nullptr, // the drag half; this car does not drag
    .leave = nullptr,
    .motion = nullptr,
    .drop = nullptr,
    .selection = device_selection,
};
// NOLINTEND(misc-include-cleaner)

/// Paints one frame: ink ground and one square whose color is the whole
/// story -- dim while the word sits home, gold once it has crossed.
void paint(const Guest& guest) noexcept {
    std::uint32_t* row =
        guest.words + (static_cast<std::size_t>(guest.active) * kStrideWords * kHeight);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            row[(static_cast<std::size_t>(y) * kStrideWords) + static_cast<std::size_t>(x)] = kInk;
        }
    }
    const bool lit = guest.copy_role ? guest.armed : guest.poured;
    const int origin = (kWidth - kSquare) / 2;
    for (int y = origin; y < origin + kSquare; ++y) {
        for (int x = origin; x < origin + kSquare; ++x) {
            row[(static_cast<std::size_t>(y) * kStrideWords) + static_cast<std::size_t>(x)] =
                lit ? kAmber : kDimmed;
        }
    }
}

/// The paste ask: one pipe, the receive word, then a bounded wait for the
/// far end to pour. What arrives prints on stdout and lights the square.
bool take_word(const Guest& guest, wl_display* display) noexcept {
    if (guest.offer == nullptr || !guest.has_selection || !guest.wants_text) {
        return false;
    }
    std::array<int, 2> ends{};
    if (::pipe(ends.data()) != 0) {
        return false;
    }
    wl_data_offer_receive(guest.offer, "text/plain;charset=utf-8", ends[1]);
    static_cast<void>(wl_display_flush(display));
    ::close(ends[1]); // the kernel carried its copy; this end has said its word
    std::array<char, 256> word{};
    std::size_t got = 0;
    while (got < word.size()) {
        struct pollfd waiter{
            .fd = ends[0],
            .events = POLLIN,
            .revents = 0,
        };
        if (::poll(&waiter, 1, 5000) <= 0) {
            break; // a quiet pipe five seconds long is the answer itself
        }
        const auto came = ::read(ends[0], word.data() + got, word.size() - got);
        if (came <= 0) {
            break; // EOF: the pour is home
        }
        got += static_cast<std::size_t>(came);
    }
    ::close(ends[0]);
    if (got == 0) {
        return false;
    }
    std::println("clip_pair: 取到 {} 字节: {}", got, std::string_view{word.data(), got});
    return true;
}

} // namespace

int main(int argc, const char* const* argv) {
    Guest guest;
    if (argc > 1 && std::strcmp(argv[1], "paste") == 0) {
        guest.copy_role = false;
    } else if (argc > 1 && std::strcmp(argv[1], "copy") != 0) {
        std::println(stderr, "clip_pair: 用法 clip_pair copy|paste");
        return 1;
    }
    wl_display* display = wl_display_connect(nullptr);
    if (display == nullptr) {
        std::println(stderr, "clip_pair: no display (set WAYLAND_DISPLAY to the desk's socket)");
        return 1;
    }
    wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &kRegistry, &guest);
    static_cast<void>(wl_display_roundtrip(display));
    wl_registry_destroy(registry);
    if (guest.compositor == nullptr || guest.shm == nullptr || guest.wm_base == nullptr ||
        guest.seat == nullptr || guest.manager == nullptr) {
        std::println(stderr, "clip_pair: the desk did not offer the five globals a car needs");
        return 1;
    }
    wl_seat_add_listener(guest.seat, &kSeat, &guest);
    xdg_wm_base_add_listener(guest.wm_base, &kWmBase, &guest);
    guest.device = wl_data_device_manager_get_data_device(guest.manager, guest.seat);
    wl_data_device_add_listener(guest.device, &kDevice, &guest);
    if (guest.copy_role) {
        guest.source = wl_data_device_manager_create_data_source(guest.manager);
        wl_data_source_add_listener(guest.source, &kSource, &guest);
        wl_data_source_offer(guest.source, "text/plain;charset=utf-8");
    }

    const int fd = ::memfd_create("clip-pair", 0);
    if (fd < 0 || ::ftruncate(fd, static_cast<off_t>(kPoolBytes)) != 0) {
        std::println(stderr, "clip_pair: no pool");
        return 1;
    }
    void* map = ::mmap(nullptr, kPoolBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        std::println(stderr, "clip_pair: no mapping");
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
    xdg_toplevel_set_title(guest.toplevel, guest.copy_role ? "COPY" : "PASTE");
    wl_surface_commit(guest.surface);
    while (!guest.configured && wl_display_dispatch(display) >= 0) {
    }

    // The face lands before the quiet loop: a guest that commits no buffer
    // is a window the desk never maps, and there would be nothing to click.
    paint(guest);
    wl_surface_attach(guest.surface, guest.buffers[guest.active], 0, 0);
    wl_surface_damage(guest.surface, 0, 0, kWidth, kHeight);
    wl_surface_commit(guest.surface);
    static_cast<void>(wl_display_flush(display));

    // The quiet loop: a car with no animation, repaint only when the word's
    // state moves. The click is the gear stick.
    bool shown = guest.copy_role ? guest.armed : guest.poured;
    while (!guest.close_requested) {
        struct pollfd waiter{
            .fd = wl_display_get_fd(display),
            .events = POLLIN,
            .revents = 0,
        };
        if (::poll(&waiter, 1, 100) == 0) {
            continue;
        }
        if (wl_display_dispatch(display) < 0) {
            break;
        }
        if (!guest.clicked) {
            continue;
        }
        guest.clicked = false;
        if (guest.copy_role) {
            wl_data_device_set_selection(guest.device, guest.source, guest.click_serial);
            guest.armed = true;
        } else {
            guest.poured = take_word(guest, display);
        }
        static_cast<void>(wl_display_flush(display));
        const bool lit = guest.copy_role ? guest.armed : guest.poured;
        if (lit != shown || guest.active == 0) {
            guest.active = 1 - guest.active;
            paint(guest);
            wl_surface_attach(guest.surface, guest.buffers[guest.active], 0, 0);
            wl_surface_damage(guest.surface, 0, 0, kWidth, kHeight);
            wl_surface_commit(guest.surface);
            static_cast<void>(wl_display_flush(display));
            shown = lit;
        }
    }
    return 0;
}
