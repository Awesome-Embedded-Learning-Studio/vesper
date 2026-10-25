#include "compositor/sparring_client.hpp"

#include "compositor/server.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>

#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-util.h>

#include "xdg-shell-client-protocol.h"

namespace vesper::testing {
namespace {

void on_global(void* data, wl_registry* /*registry*/, std::uint32_t name, const char* interface,
               std::uint32_t version) noexcept {
    static_cast<SparringClient*>(data)->record_global(name, interface, version);
}

void on_global_remove(void* /*data*/, wl_registry* /*registry*/, std::uint32_t /*name*/) noexcept {
    // Globals do not leave while a judgment runs; the heart has no hotplug
    // story and neither does the desk.
}

void on_sync_done(void* data, wl_callback* /*callback*/, std::uint32_t /*serial*/) noexcept {
    static_cast<SparringClient*>(data)->note_sync_done();
}

void on_frame_done(void* data, wl_callback* callback, std::uint32_t /*callback_data*/) noexcept {
    // The done word retires the callback in both directions: the proxy is
    // the client's to destroy, and leaving it behind leaks it (the server
    // already dropped its half).
    wl_callback_destroy(callback);
    static_cast<SparringClient*>(data)->note_frame_done();
}

void on_buffer_release(void* data, wl_buffer* /*buffer*/) noexcept {
    static_cast<SparringClient*>(data)->note_release();
}

void on_wm_base_ping(void* /*data*/, xdg_wm_base* wm_base, std::uint32_t serial) noexcept {
    // A ping asks whether the guest lives; the answer is one word.
    xdg_wm_base_pong(wm_base, serial);
}

void on_xdg_surface_configure(void* data, xdg_surface* /*xdg_surface*/,
                              std::uint32_t serial) noexcept {
    static_cast<SparringClient*>(data)->note_configure(serial);
}

void on_toplevel_configure(void* data, xdg_toplevel* /*toplevel*/, std::int32_t width,
                           std::int32_t height, wl_array* /*states*/) noexcept {
    static_cast<SparringClient*>(data)->note_toplevel_configure(width, height);
}

void on_toplevel_close(void* data, xdg_toplevel* /*toplevel*/) noexcept {
    static_cast<SparringClient*>(data)->note_close();
}

const xdg_wm_base_listener kWmBase = {
    .ping = on_wm_base_ping,
};

const xdg_surface_listener kXdgSurface = {
    .configure = on_xdg_surface_configure,
};

void on_pointer_enter(void* data, wl_pointer* /*pointer*/, std::uint32_t /*serial*/,
                      wl_surface* /*surface*/, wl_fixed_t sx, wl_fixed_t sy) noexcept {
    static_cast<SparringClient*>(data)->note_pointer_enter(wl_fixed_to_int(sx),
                                                           wl_fixed_to_int(sy));
}

void on_pointer_leave(void* data, wl_pointer* /*pointer*/, std::uint32_t /*serial*/,
                      wl_surface* /*surface*/) noexcept {
    static_cast<SparringClient*>(data)->note_pointer_leave();
}

void on_pointer_motion(void* data, wl_pointer* /*pointer*/, std::uint32_t /*time*/, wl_fixed_t sx,
                       wl_fixed_t sy) noexcept {
    static_cast<SparringClient*>(data)->record_motion(wl_fixed_to_int(sx), wl_fixed_to_int(sy));
}

void on_pointer_button(void* data, wl_pointer* /*pointer*/, std::uint32_t /*serial*/,
                       std::uint32_t /*time*/, std::uint32_t button, std::uint32_t state) noexcept {
    static_cast<SparringClient*>(data)->record_button(button,
                                                      state == WL_POINTER_BUTTON_STATE_PRESSED);
}

void on_pointer_axis(void* /*data*/, wl_pointer* /*pointer*/, std::uint32_t /*time*/,
                     std::uint32_t /*axis*/, wl_fixed_t /*value*/) noexcept {
    // The high-resolution word below carries the detents this partner reads.
}

void on_pointer_frame(void* /*data*/, wl_pointer* /*pointer*/) noexcept {
    // The group's close; each word recorded itself.
}

void on_pointer_axis_value120(void* data, wl_pointer* /*pointer*/, std::uint32_t axis,
                              std::int32_t value) noexcept {
    static_cast<SparringClient*>(data)->record_wheel(axis, value);
}

void on_pointer_axis_source(void* /*data*/, wl_pointer* /*pointer*/,
                            std::uint32_t /*source*/) noexcept {}

void on_pointer_axis_stop(void* /*data*/, wl_pointer* /*pointer*/, std::uint32_t /*time*/,
                          std::uint32_t /*axis*/) noexcept {}

const wl_pointer_listener kPointer = {
    .enter = on_pointer_enter,
    .leave = on_pointer_leave,
    .motion = on_pointer_motion,
    .button = on_pointer_button,
    .axis = on_pointer_axis,
    .frame = on_pointer_frame,
    .axis_source = on_pointer_axis_source,
    .axis_stop = on_pointer_axis_stop,
    .axis_discrete = nullptr, // the seat binds at 5; the discrete words are older
    .axis_value120 = on_pointer_axis_value120,
    .axis_relative_direction = nullptr,
    .warp = nullptr,
};

void on_keyboard_keymap(void* data, wl_keyboard* /*keyboard*/, std::uint32_t /*format*/,
                        std::int32_t fd, std::uint32_t size) noexcept {
    // The partner reads raw codes and never parses the map; the file is
    // closed unread, the size remembered as the arrival's witness.
    ::close(fd);
    static_cast<SparringClient*>(data)->note_keymap(size);
}

void on_keyboard_enter(void* data, wl_keyboard* /*keyboard*/, std::uint32_t /*serial*/,
                       wl_surface* /*surface*/, wl_array* /*keys*/) noexcept {
    static_cast<SparringClient*>(data)->note_keyboard_enter();
}

void on_keyboard_leave(void* data, wl_keyboard* /*keyboard*/, std::uint32_t /*serial*/,
                       wl_surface* /*surface*/) noexcept {
    static_cast<SparringClient*>(data)->note_keyboard_leave();
}

void on_keyboard_key(void* data, wl_keyboard* /*keyboard*/, std::uint32_t /*serial*/,
                     std::uint32_t /*time*/, std::uint32_t key, std::uint32_t state) noexcept {
    static_cast<SparringClient*>(data)->record_key(key, state == WL_KEYBOARD_KEY_STATE_PRESSED);
}

void on_keyboard_modifiers(void* data, wl_keyboard* /*keyboard*/, std::uint32_t /*serial*/,
                           std::uint32_t depressed, std::uint32_t /*latched*/,
                           std::uint32_t /*locked*/, std::uint32_t /*group*/) noexcept {
    static_cast<SparringClient*>(data)->note_modifiers(depressed);
}

void on_keyboard_repeat(void* /*data*/, wl_keyboard* /*keyboard*/, std::int32_t /*rate*/,
                        std::int32_t /*delay*/) noexcept {
    // Repeat is the desk's later layer; the word is heard and filed.
}

const wl_keyboard_listener kKeyboard = {
    .keymap = on_keyboard_keymap,
    .enter = on_keyboard_enter,
    .leave = on_keyboard_leave,
    .key = on_keyboard_key,
    .modifiers = on_keyboard_modifiers,
    .repeat_info = on_keyboard_repeat,
};

void on_seat_capabilities(void* data, wl_seat* seat, std::uint32_t capabilities) noexcept {
    auto* client = static_cast<SparringClient*>(data);
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0 && client->pointer_proxy() == nullptr) {
        wl_pointer* pointer = wl_seat_get_pointer(seat);
        client->adopt_pointer(pointer);
        wl_pointer_add_listener(pointer, &kPointer, data);
    }
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0 && client->keyboard_proxy() == nullptr) {
        wl_keyboard* keyboard = wl_seat_get_keyboard(seat);
        client->adopt_keyboard(keyboard);
        wl_keyboard_add_listener(keyboard, &kKeyboard, data);
    }
}

void on_seat_name(void* /*data*/, wl_seat* /*seat*/, const char* /*name*/) noexcept {
    // Which seat this is carries nothing the judgments read.
}

const wl_seat_listener kSeat = {
    .capabilities = on_seat_capabilities,
    .name = on_seat_name,
};

const xdg_toplevel_listener kToplevel = {
    .configure = on_toplevel_configure,
    .close = on_toplevel_close,
    .configure_bounds = nullptr, // sizing advice the guest never follows
    .wm_capabilities = nullptr,  // what the desk allows; the guest takes it
};

const wl_registry_listener kRegistry = {
    .global = on_global,
    .global_remove = on_global_remove,
};

const wl_callback_listener kSync = {
    .done = on_sync_done,
};

const wl_callback_listener kFrame = {
    .done = on_frame_done,
};

const wl_buffer_listener kBuffer = {
    .release = on_buffer_release,
};

} // namespace

SparringClient::~SparringClient() {
    disconnect();
}

bool SparringClient::connect_fd(int fd) noexcept {
    if (display_ != nullptr) {
        ::close(fd); // one client per object; the newcomer is the wrong one
        return false;
    }
    display_ = wl_display_connect_to_fd(fd);
    if (display_ == nullptr) {
        ::close(fd); // the library leaves the fd open on refusal
        return false;
    }
    return true;
}

void SparringClient::disconnect() noexcept {
    // The goodbye order is the protocol's own: buffers first (they may
    // outlive their pool), then the pool, then shm. On a connection the
    // server already killed the destroy words land nowhere, but the proxies
    // free either way -- the client library leaves them to their owner.
    for (std::size_t i = 0; i < buffer_count_; ++i) {
        wl_proxy_destroy(reinterpret_cast<wl_proxy*>(buffers_[i]));
    }
    buffer_count_ = 0;
    if (pool_ != nullptr) {
        wl_proxy_destroy(reinterpret_cast<wl_proxy*>(pool_));
        pool_ = nullptr;
    }
    if (shm_ != nullptr) {
        wl_proxy_destroy(reinterpret_cast<wl_proxy*>(shm_));
        shm_ = nullptr;
    }
    if (pool_words_ != nullptr) {
        ::munmap(pool_words_, pool_bytes_);
        pool_words_ = nullptr;
        pool_bytes_ = 0;
    }
    if (pool_fd_ >= 0) {
        ::close(pool_fd_);
        pool_fd_ = -1;
    }
    if (keyboard_ != nullptr) {
        wl_proxy_destroy(reinterpret_cast<wl_proxy*>(keyboard_));
        keyboard_ = nullptr;
    }
    if (pointer_ != nullptr) {
        wl_proxy_destroy(reinterpret_cast<wl_proxy*>(pointer_));
        pointer_ = nullptr;
    }
    if (seat_ != nullptr) {
        wl_proxy_destroy(reinterpret_cast<wl_proxy*>(seat_));
        seat_ = nullptr;
    }
    if (toplevel_ != nullptr) {
        wl_proxy_destroy(reinterpret_cast<wl_proxy*>(toplevel_));
        toplevel_ = nullptr;
    }
    if (xdg_surface_ != nullptr) {
        wl_proxy_destroy(reinterpret_cast<wl_proxy*>(xdg_surface_));
        xdg_surface_ = nullptr;
    }
    if (wm_base_ != nullptr) {
        wl_proxy_destroy(reinterpret_cast<wl_proxy*>(wm_base_));
        wm_base_ = nullptr;
    }
    if (surface_ != nullptr) {
        wl_proxy_destroy(reinterpret_cast<wl_proxy*>(surface_));
        surface_ = nullptr;
    }
    if (compositor_ != nullptr) {
        wl_proxy_destroy(reinterpret_cast<wl_proxy*>(compositor_));
        compositor_ = nullptr;
    }
    if (display_ != nullptr) {
        wl_display_disconnect(display_);
        display_ = nullptr;
    }
}

bool SparringClient::survey_globals(compositor::Server& server) noexcept {
    if (display_ == nullptr) {
        return false;
    }
    global_count_ = 0;
    synced_ = false;
    wl_registry* registry = wl_display_get_registry(display_);
    wl_registry_add_listener(registry, &kRegistry, this);
    wl_callback* sync = wl_display_sync(display_);
    wl_callback_add_listener(sync, &kSync, this);
    // A survey is an exchange with one word of lag (registry words, then the
    // sync answer), so a bounded ladder of settles covers it without ever
    // parking on a dead connection.
    bool ok = false;
    for (int round = 0; round < 8; ++round) {
        if (!settle(server)) {
            break;
        }
        if (synced_) {
            ok = true;
            break;
        }
    }
    wl_callback_destroy(sync);
    wl_registry_destroy(registry);
    return ok;
}

std::uint32_t SparringClient::version_of(const char* interface) const noexcept {
    const Global* found = find_global(interface);
    return found != nullptr ? found->version : 0;
}

const SparringClient::Global* SparringClient::find_global(const char* interface) const noexcept {
    for (std::size_t i = 0; i < global_count_; ++i) {
        if (std::strcmp(globals_[i].interface.data(), interface) == 0) {
            return &globals_[i];
        }
    }
    return nullptr;
}

bool SparringClient::settle(compositor::Server& server, int rounds) noexcept {
    if (display_ == nullptr) {
        return false;
    }
    for (int round = 0; round < rounds; ++round) {
        static_cast<void>(wl_display_flush(display_));
        if (!server.dispatch(0)) {
            return false;
        }
        server.flush_clients();
        if (!pump()) {
            return false;
        }
    }
    return true;
}

bool SparringClient::pump() noexcept {
    // The documented nonblocking read: dispatch whatever is buffered, then
    // read only what a zero-wait poll says arrived, then dispatch again.
    while (wl_display_prepare_read(display_) != 0) {
        if (wl_display_dispatch_pending(display_) < 0) {
            return false;
        }
    }
    struct pollfd waiter{
        .fd = wl_display_get_fd(display_),
        .events = POLLIN,
        .revents = 0,
    };
    const int ready = ::poll(&waiter, 1, 0);
    if (ready < 0) {
        wl_display_cancel_read(display_);
        return errno == EINTR; // an interrupted wait is a quiet round
    }
    if (ready > 0) {
        if (wl_display_read_events(display_) < 0 && errno != EAGAIN) {
            return false;
        }
    } else {
        wl_display_cancel_read(display_);
    }
    if (wl_display_dispatch_pending(display_) < 0) {
        return false;
    }
    return wl_display_get_error(display_) == 0;
}

void SparringClient::record_global(std::uint32_t name, const char* interface,
                                   std::uint32_t version) noexcept {
    if (global_count_ >= globals_.size()) {
        return; // the heart's four globals fit with room to spare
    }
    Global& entry = globals_[global_count_++];
    entry.name = name;
    entry.version = version;
    std::strncpy(entry.interface.data(), interface, entry.interface.size() - 1);
}

void SparringClient::note_sync_done() noexcept {
    synced_ = true;
}

void SparringClient::note_frame_done() noexcept {
    ++frame_done_count_;
}

void SparringClient::note_release() noexcept {
    ++release_count_;
}

void SparringClient::note_configure(std::uint32_t serial) noexcept {
    // The arrival is only news when the ack lane waits on it: the ack and
    // the flag travel together, so an unconfigured lane stays honestly
    // unconfigured.
    if (xdg_auto_ack_) {
        xdg_surface_ack_configure(xdg_surface_, serial);
        xdg_configured_ = true;
    }
}

void SparringClient::note_close() noexcept {
    close_requested_ = true;
}

void SparringClient::note_toplevel_configure(std::int32_t width, std::int32_t height) noexcept {
    last_configure_w_ = width;
    last_configure_h_ = height;
}

void SparringClient::record_motion(std::int32_t x, std::int32_t y) noexcept {
    if (motion_count_ < motions_.size()) {
        motions_[motion_count_++] = PointWord{.x = x, .y = y};
    }
}

void SparringClient::record_button(std::uint32_t code, bool pressed) noexcept {
    if (button_count_ < buttons_.size()) {
        buttons_[button_count_++] = ButtonWord{.code = code, .pressed = pressed};
    }
}

void SparringClient::record_wheel(std::uint32_t axis, std::int32_t value) noexcept {
    if (wheel_count_ < wheels_.size()) {
        wheels_[wheel_count_++] = WheelWord{.axis = axis, .value = value};
    }
}

void SparringClient::ask_own_cursor() noexcept {
    if (pointer_ != nullptr) {
        wl_pointer_set_cursor(pointer_, 0, surface_, 0, 0);
    }
}

void SparringClient::record_key(std::uint32_t code, bool pressed) noexcept {
    if (key_count_ < keys_.size()) {
        keys_[key_count_++] = KeyWord{.code = code, .pressed = pressed};
    }
}

void SparringClient::note_modifiers(std::uint32_t mask) noexcept {
    last_modifiers_ = mask;
}

void SparringClient::note_keymap(std::uint32_t bytes) noexcept {
    keymap_bytes_ = bytes;
}

void SparringClient::note_pointer_enter(std::int32_t x, std::int32_t y) noexcept {
    ++pointer_enters_;
    record_motion(x, y);
}

void SparringClient::note_pointer_leave() noexcept {
    ++pointer_leaves_;
}

void SparringClient::note_keyboard_enter() noexcept {
    ++keyboard_enters_;
}

void SparringClient::note_keyboard_leave() noexcept {
    ++keyboard_leaves_;
}

bool SparringClient::alive() const noexcept {
    return display_ != nullptr && wl_display_get_error(display_) == 0;
}

bool SparringClient::bind_compositor() noexcept {
    if (display_ == nullptr || compositor_ != nullptr) {
        return false;
    }
    const Global* found = find_global("wl_compositor");
    if (found == nullptr) {
        return false;
    }
    wl_registry* registry = wl_display_get_registry(display_);
    compositor_ = static_cast<wl_compositor*>(
        wl_registry_bind(registry, found->name, &wl_compositor_interface, found->version));
    wl_registry_destroy(registry);
    return compositor_ != nullptr;
}

bool SparringClient::bind_wm_base() noexcept {
    if (display_ == nullptr || wm_base_ != nullptr) {
        return false;
    }
    const Global* found = find_global("xdg_wm_base");
    if (found == nullptr) {
        return false;
    }
    wl_registry* registry = wl_display_get_registry(display_);
    wm_base_ = static_cast<xdg_wm_base*>(
        wl_registry_bind(registry, found->name, &xdg_wm_base_interface, found->version));
    wl_registry_destroy(registry);
    if (wm_base_ == nullptr) {
        return false;
    }
    xdg_wm_base_add_listener(wm_base_, &kWmBase, this);
    return true;
}

bool SparringClient::make_toplevel(compositor::Server& server, const char* title,
                                   bool auto_ack) noexcept {
    if (wm_base_ == nullptr || surface_ == nullptr || xdg_surface_ != nullptr) {
        return false;
    }
    xdg_auto_ack_ = auto_ack;
    xdg_configured_ = false;
    xdg_surface_ = xdg_wm_base_get_xdg_surface(wm_base_, surface_);
    xdg_surface_add_listener(xdg_surface_, &kXdgSurface, this);
    toplevel_ = xdg_surface_get_toplevel(xdg_surface_);
    xdg_toplevel_add_listener(toplevel_, &kToplevel, this);
    xdg_toplevel_set_title(toplevel_, title);
    wl_surface_commit(surface_); // the empty commit that asks for configure
    for (int round = 0; round < 8; ++round) {
        if (!settle(server)) {
            return false;
        }
        if (!auto_ack || xdg_configured_) {
            return true;
        }
    }
    return false; // no configure ever came; the desk stopped talking
}

wl_surface* SparringClient::make_surface() noexcept {
    if (compositor_ == nullptr) {
        return nullptr;
    }
    surface_ = wl_compositor_create_surface(compositor_);
    return surface_;
}

void SparringClient::attach(wl_buffer* buffer) noexcept {
    if (surface_ != nullptr) {
        wl_surface_attach(surface_, buffer, 0, 0);
    }
}

void SparringClient::damage(int x, int y, int width, int height) noexcept {
    if (surface_ != nullptr) {
        wl_surface_damage(surface_, x, y, width, height);
    }
}

void SparringClient::commit() noexcept {
    if (surface_ != nullptr) {
        wl_surface_commit(surface_);
    }
}

void SparringClient::request_frame() noexcept {
    if (surface_ != nullptr) {
        wl_callback* callback = wl_surface_frame(surface_);
        wl_callback_add_listener(callback, &kFrame, this);
    }
}

bool SparringClient::bind_shm() noexcept {
    if (display_ == nullptr || shm_ != nullptr) {
        return false;
    }
    const Global* found = find_global("wl_shm");
    if (found == nullptr) {
        return false;
    }
    // A fresh registry just for the bind: the survey's own registry is gone,
    // and the protocol allows as many as the guest likes.
    wl_registry* registry = wl_display_get_registry(display_);
    shm_ = static_cast<wl_shm*>(
        wl_registry_bind(registry, found->name, &wl_shm_interface, found->version));
    wl_registry_destroy(registry);
    return shm_ != nullptr;
}

bool SparringClient::make_pool(std::size_t bytes) noexcept {
    if (shm_ == nullptr || pool_words_ != nullptr) {
        return false;
    }
    const int fd = ::memfd_create("vesper-sparring", 0);
    if (fd < 0) {
        return false;
    }
    if (::ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
        ::close(fd);
        return false;
    }
    void* map = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        ::close(fd);
        return false;
    }
    pool_ = wl_shm_create_pool(shm_, fd, static_cast<std::int32_t>(bytes));
    if (pool_ == nullptr) {
        ::close(fd);
        ::munmap(map, bytes);
        return false;
    }
    pool_fd_ = fd; // the resize word needs the file to still be growable
    pool_words_ = static_cast<std::uint32_t*>(map);
    pool_bytes_ = bytes;
    return true;
}

bool SparringClient::resize_pool(std::size_t bytes) noexcept {
    // Grow the file first (the spec makes that the guest's job), then the
    // mapping, then the word to the server -- a refused link in that chain
    // leaves the earlier ones grown but the pool still honest at its old
    // size, which the next exchange surfaces.
    if (pool_ == nullptr || bytes == 0 || ::ftruncate(pool_fd_, static_cast<off_t>(bytes)) != 0) {
        return false;
    }
    void* moved = ::mremap(pool_words_, pool_bytes_, bytes, MREMAP_MAYMOVE);
    if (moved == MAP_FAILED) {
        return false;
    }
    pool_words_ = static_cast<std::uint32_t*>(moved);
    pool_bytes_ = bytes;
    wl_shm_pool_resize(pool_, static_cast<std::int32_t>(bytes));
    return true;
}

wl_buffer* SparringClient::make_buffer(std::size_t offset_words, int width, int height,
                                       std::size_t stride_words, std::uint32_t format) noexcept {
    if (pool_ == nullptr || buffer_count_ >= buffers_.size()) {
        return nullptr;
    }
    wl_buffer* buffer =
        wl_shm_pool_create_buffer(pool_, static_cast<std::int32_t>(offset_words * 4U), width,
                                  height, static_cast<std::int32_t>(stride_words * 4U), format);
    if (buffer != nullptr) {
        wl_buffer_add_listener(buffer, &kBuffer, this); // the release words land here
        buffers_[buffer_count_++] = buffer;             // disconnect's goodbye finds them here
    }
    return buffer;
}

bool SparringClient::bind_seat(compositor::Server& server) noexcept {
    if (display_ == nullptr || seat_ != nullptr) {
        return false;
    }
    const Global* found = find_global("wl_seat");
    if (found == nullptr) {
        return false;
    }
    wl_registry* registry = wl_display_get_registry(display_);
    seat_ = static_cast<wl_seat*>(
        wl_registry_bind(registry, found->name, &wl_seat_interface, found->version));
    wl_registry_destroy(registry);
    if (seat_ == nullptr) {
        return false;
    }
    wl_seat_add_listener(seat_, &kSeat, this);
    // Two exchanges: the capabilities word arrives on the first, the
    // pointer and keyboard objects stand on the second.
    return settle(server, 2) && pointer_ != nullptr && keyboard_ != nullptr;
}

const SparringClient::KeyWord* SparringClient::key_at(std::size_t index) const noexcept {
    return index < key_count_ ? &keys_[index] : nullptr;
}

const SparringClient::PointWord* SparringClient::motion_at(std::size_t index) const noexcept {
    return index < motion_count_ ? &motions_[index] : nullptr;
}

const SparringClient::ButtonWord* SparringClient::button_at(std::size_t index) const noexcept {
    return index < button_count_ ? &buttons_[index] : nullptr;
}

const SparringClient::WheelWord* SparringClient::wheel_at(std::size_t index) const noexcept {
    return index < wheel_count_ ? &wheels_[index] : nullptr;
}

void SparringClient::retire_toplevel() noexcept {
    if (toplevel_ != nullptr) {
        xdg_toplevel* toplevel = toplevel_;
        toplevel_ = nullptr; // disconnect must not destroy it a second time
        xdg_toplevel_destroy(toplevel);
    }
    if (xdg_surface_ != nullptr) {
        xdg_surface* xdg = xdg_surface_;
        xdg_surface_ = nullptr;
        xdg_surface_destroy(xdg);
    }
}

void SparringClient::retire_surface() noexcept {
    if (surface_ != nullptr) {
        wl_surface* surface = surface_;
        surface_ = nullptr; // disconnect must not destroy it a second time
        wl_surface_destroy(surface);
    }
}

void SparringClient::retire_buffer(wl_buffer* buffer) noexcept {
    for (std::size_t i = 0; i < buffer_count_; ++i) {
        if (buffers_[i] == buffer) {
            buffers_[i] = buffers_[buffer_count_ - 1];
            --buffer_count_;
            break;
        }
    }
    wl_buffer_destroy(buffer);
}

} // namespace vesper::testing
