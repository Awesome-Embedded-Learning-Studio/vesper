#include "compositor/seat.hpp"

#include <unistd.h>

#include <cstdint>
#include <string_view>

#include <sys/mman.h>

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>

#include "compositor/surface.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/bindings/events/modifiers.hpp"

namespace vesper::compositor {
namespace {

// The one keymap the seat ever ships: the standard evdev-plus-qwerty shape
// spelled the way weston and friends generate it, a text keymap any
// keymap-reading guest can parse and any raw-code guest can ignore. The
// desk never interprets it itself -- key words travel as Linux native
// codes, and the keymap is the guests' own tool.
constexpr std::string_view kKeymap = "xkb_keymap {\n"
                                     "  xkb_keycodes { include \"evdev+aliases(qwerty)\" };\n"
                                     "  xkb_types    { include \"complete\" };\n"
                                     "  xkb_compat   { include \"complete\" };\n"
                                     "  xkb_symbols  { include \"pc+us+inet(evdev)\" };\n"
                                     "};\n";

// The xkb modifier masks for the four states the desk's word carries.
constexpr std::uint32_t kShiftMask = 0x1;
constexpr std::uint32_t kControlMask = 0x4;
constexpr std::uint32_t kAltMask = 0x8;
constexpr std::uint32_t kSuperMask = 0x40;

Seat* seat_of(wl_resource* resource) noexcept {
    return static_cast<Seat*>(wl_resource_get_user_data(resource));
}

// The pointer and keyboard objects answer nothing themselves; their life is
// the whole conversation, and their goodbye is Wiring's to keep below.
void input_object_destroy_request(wl_client* /*client*/, wl_resource* resource) noexcept {
    wl_resource_destroy(resource);
}

const struct wl_seat_interface kSeatTable = {
    .get_pointer = Seat::Wiring::seat_get_pointer,
    .get_keyboard = Seat::Wiring::seat_get_keyboard,
    .get_touch = Seat::Wiring::seat_get_touch,
    .release = Seat::Wiring::seat_release,
};

// The guest asks to draw its own cursor (an I-beam over text, a resize
// arrow on an edge). The desk keeps painting its own pointer for now, but
// the word must land somewhere: libwayland aborts the whole compositor
// when a request arrives and its slot in the table is null.
void pointer_set_cursor(wl_client* /*client*/, wl_resource* /*resource*/, std::uint32_t /*serial*/,
                        wl_resource* /*surface*/, std::int32_t /*hotspot_x*/,
                        std::int32_t /*hotspot_y*/) noexcept {}

const struct wl_pointer_interface kPointerTable = {
    .set_cursor = pointer_set_cursor,
    .release = input_object_destroy_request,
};

const struct wl_keyboard_interface kKeyboardTable = {
    .release = input_object_destroy_request,
};

const struct wl_touch_interface kTouchTable = {
    .release = input_object_destroy_request,
};

} // namespace

// --- the desk's half ---------------------------------------------------------

Seat::Seat() noexcept {
    // The keymap file is written once and dup'ed per keyboard; the guests
    // close their copies, the original stays the seat's.
    keymap_fd_ = ::memfd_create("vesper-keymap", 0);
    if (keymap_fd_ >= 0) {
        if (::write(keymap_fd_, kKeymap.data(), kKeymap.size()) < 0) {
            ::close(keymap_fd_);
            keymap_fd_ = -1;
        } else {
            keymap_bytes_ = kKeymap.size();
        }
    }
}

Seat::~Seat() {
    if (keymap_fd_ >= 0) {
        ::close(keymap_fd_);
    }
}

void Seat::note_time_ms(std::uint32_t ms) noexcept {
    time_ms_ = ms;
}

Seat::Binding* Seat::binding_of_client(wl_client* client) noexcept {
    for (auto& binding : bindings_) {
        if (binding.client == client) {
            return &binding;
        }
    }
    return nullptr;
}

Seat::Binding* Seat::binding_of_surface(Surface* surface) noexcept {
    if (surface == nullptr) {
        return nullptr;
    }
    // A surface without its resource is a dead one; the desk clears focus
    // on unmap, so this only guards a race the single thread cannot run.
    wl_resource* resource = surface->resource();
    if (resource == nullptr) {
        return nullptr;
    }
    return binding_of_client(wl_resource_get_client(resource));
}

Seat::Binding* Seat::pointer_binding() noexcept {
    return pointer_focus_ != nullptr ? binding_of_surface(pointer_focus_) : nullptr;
}

Seat::Binding* Seat::keyboard_binding() noexcept {
    return keyboard_focus_ != nullptr ? binding_of_surface(keyboard_focus_) : nullptr;
}

std::uint32_t Seat::next_serial() noexcept {
    return ++serial_;
}

void Seat::pointer_enter(Surface* surface, visor::base::PointI at) noexcept {
    if (surface == pointer_focus_) {
        pointer_motion(at); // the same face: the guest hears the move only
        return;
    }
    if (pointer_focus_ != nullptr) {
        if (const Seat::Binding* old = binding_of_surface(pointer_focus_);
            old != nullptr && old->pointer != nullptr) {
            wl_pointer_send_leave(old->pointer, next_serial(), pointer_focus_->resource());
        }
    }
    pointer_focus_ = surface;
    if (surface == nullptr) {
        return;
    }
    if (const Seat::Binding* fresh = binding_of_surface(surface);
        fresh != nullptr && fresh->pointer != nullptr) {
        wl_pointer_send_enter(fresh->pointer, next_serial(), surface->resource(),
                              wl_fixed_from_int(at.x), wl_fixed_from_int(at.y));
    }
}

void Seat::pointer_leave() noexcept {
    pointer_enter(nullptr, visor::base::PointI{0, 0});
}

void Seat::pointer_motion(visor::base::PointI at) noexcept {
    if (const Seat::Binding* binding = pointer_binding(); binding != nullptr) {
        wl_pointer_send_motion(binding->pointer, time_ms_, wl_fixed_from_int(at.x),
                               wl_fixed_from_int(at.y));
    }
}

void Seat::pointer_button(std::uint32_t button, bool pressed) noexcept {
    if (const Seat::Binding* binding = pointer_binding(); binding != nullptr) {
        wl_pointer_send_button(binding->pointer, next_serial(), time_ms_, button,
                               pressed ? WL_POINTER_BUTTON_STATE_PRESSED
                                       : WL_POINTER_BUTTON_STATE_RELEASED);
    }
}

void Seat::pointer_wheel(std::int32_t dx, std::int32_t dy) noexcept {
    const Seat::Binding* binding = pointer_binding();
    if (binding == nullptr) {
        return;
    }
    // One high-resolution word per axis, each closed by a frame: the v5
    // grouping the pointer's own spec draws.
    if (dx != 0) {
        wl_pointer_send_axis_value120(binding->pointer, WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                                      dx * 120);
        wl_pointer_send_frame(binding->pointer);
    }
    if (dy != 0) {
        wl_pointer_send_axis_value120(binding->pointer, WL_POINTER_AXIS_VERTICAL_SCROLL, dy * 120);
        wl_pointer_send_frame(binding->pointer);
    }
}

Surface* Seat::pointer_focus() const noexcept {
    return pointer_focus_;
}

void Seat::keyboard_enter(Surface* surface) noexcept {
    if (surface == keyboard_focus_) {
        return;
    }
    if (keyboard_focus_ != nullptr) {
        if (const Seat::Binding* old = binding_of_surface(keyboard_focus_);
            old != nullptr && old->keyboard != nullptr) {
            wl_keyboard_send_leave(old->keyboard, next_serial(), keyboard_focus_->resource());
        }
    }
    keyboard_focus_ = surface;
    if (surface == nullptr) {
        return;
    }
    if (const Seat::Binding* fresh = binding_of_surface(surface);
        fresh != nullptr && fresh->keyboard != nullptr) {
        struct wl_array keys; // no keys are down on a crossing the desk reports
        wl_array_init(&keys);
        wl_keyboard_send_enter(fresh->keyboard, next_serial(), surface->resource(), &keys);
        wl_array_release(&keys);
    }
}

void Seat::key(std::uint32_t code, bool pressed) noexcept {
    // The wire speaks evdev: the arrows take the desk's named values back
    // to their key numbers, everything else passes through untouched -- the
    // text-code fold back lives at the canvas, the desk's own gate, not on
    // the protocol side.
    if (const Seat::Binding* binding = keyboard_binding(); binding != nullptr) {
        wl_keyboard_send_key(binding->keyboard, next_serial(), time_ms_, evdev_keycode(code),
                             pressed ? WL_KEYBOARD_KEY_STATE_PRESSED
                                     : WL_KEYBOARD_KEY_STATE_RELEASED);
    }
}

void Seat::keyboard_leave() noexcept {
    keyboard_enter(nullptr);
}

void Seat::set_modifiers(visor::host::Modifiers modifiers) noexcept {
    std::uint32_t mask = 0;
    if (modifiers.shift) {
        mask |= kShiftMask;
    }
    if (modifiers.ctrl) {
        mask |= kControlMask;
    }
    if (modifiers.alt) {
        mask |= kAltMask;
    }
    if (modifiers.super) {
        mask |= kSuperMask;
    }
    if (const Seat::Binding* binding = keyboard_binding(); binding != nullptr) {
        wl_keyboard_send_modifiers(binding->keyboard, next_serial(), mask, 0, 0, 0);
    }
}

Surface* Seat::keyboard_focus() const noexcept {
    return keyboard_focus_;
}

// --- the Wiring, the protocol tables' hands -----------------------------------

/// The binding a seat-side resource belongs to: the seat, the pointer
/// or the keyboard resource, whichever slot holds it.
Seat::Binding* Seat::Wiring::binding_of_resource(Seat* seat, wl_resource* resource) noexcept {
    for (auto& binding : seat->bindings_) {
        if (binding.seat == resource || binding.pointer == resource ||
            binding.keyboard == resource) {
            return &binding;
        }
    }
    return nullptr;
}

void Seat::Wiring::pointer_destroyed(wl_resource* resource) noexcept {
    Seat::Binding* binding = binding_of_resource(seat_of(resource), resource);
    if (binding != nullptr) {
        binding->pointer = nullptr;
    }
}

void Seat::Wiring::keyboard_destroyed(wl_resource* resource) noexcept {
    Seat::Binding* binding = binding_of_resource(seat_of(resource), resource);
    if (binding != nullptr) {
        binding->keyboard = nullptr;
    }
}

void Seat::Wiring::seat_destroyed(wl_resource* resource) noexcept {
    // The seat resource leaving takes the whole binding with it;
    // pointer and keyboard resources outliving their seat is a guest's
    // own mess, not one this desk keeps books for.
    Seat::Binding* binding = binding_of_resource(seat_of(resource), resource);
    if (binding != nullptr) {
        *binding = Seat::Binding{};
    }
}

void Seat::Wiring::bind(wl_client* client, void* data, std::uint32_t version,
                        std::uint32_t id) noexcept {
    auto* seat = static_cast<Seat*>(data);
    wl_resource* resource =
        wl_resource_create(client, &wl_seat_interface, static_cast<int>(version), id);
    if (resource == nullptr) {
        wl_client_post_no_memory(client);
        return;
    }
    // One binding per client: a second bind from the same client is a
    // refill of the slot it already owns.
    Seat::Binding* binding = seat->binding_of_client(client);
    if (binding == nullptr) {
        for (auto& slot : seat->bindings_) {
            if (slot.client == nullptr) {
                binding = &slot;
                break;
            }
        }
    }
    if (binding == nullptr) {
        wl_resource_destroy(resource);
        wl_client_post_no_memory(client);
        return;
    }
    binding->client = client;
    binding->seat = resource;
    wl_resource_set_implementation(resource, &kSeatTable, seat, &Seat::Wiring::seat_destroyed);
    // The C2 seat has both hands: the pointer and the keyboard, no
    // touch (a touch object is still answered, quietly, below).
    wl_seat_send_capabilities(resource, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
    if (version >= WL_SEAT_NAME_SINCE_VERSION) {
        wl_seat_send_name(resource, "vesper");
    }
}

void Seat::Wiring::seat_get_pointer(wl_client* /*client*/, wl_resource* seat_resource,
                                    std::uint32_t id) noexcept {
    Seat* seat = seat_of(seat_resource);
    Seat::Binding* binding = seat->binding_of_client(wl_resource_get_client(seat_resource));
    if (binding == nullptr) {
        return;
    }
    wl_resource* resource =
        wl_resource_create(wl_resource_get_client(seat_resource), &wl_pointer_interface,
                           wl_resource_get_version(seat_resource), id);
    if (resource == nullptr) {
        wl_client_post_no_memory(wl_resource_get_client(seat_resource));
        return;
    }
    binding->pointer = resource;
    wl_resource_set_implementation(resource, &kPointerTable, seat,
                                   &Seat::Wiring::pointer_destroyed);
}

void Seat::Wiring::seat_get_keyboard(wl_client* /*client*/, wl_resource* seat_resource,
                                     std::uint32_t id) noexcept {
    wl_client* client = wl_resource_get_client(seat_resource);
    Seat* seat = seat_of(seat_resource);
    Seat::Binding* binding = seat->binding_of_client(client);
    if (binding == nullptr) {
        return;
    }
    wl_resource* resource = wl_resource_create(client, &wl_keyboard_interface,
                                               wl_resource_get_version(seat_resource), id);
    if (resource == nullptr) {
        wl_client_post_no_memory(client);
        return;
    }
    binding->keyboard = resource;
    wl_resource_set_implementation(resource, &kKeyboardTable, seat,
                                   &Seat::Wiring::keyboard_destroyed);
    // The keymap word rides with the object itself; a seat that could
    // not build its keymap file still speaks raw codes, and the guest
    // decides what that is worth.
    if (seat->keymap_fd_ >= 0) {
        const int fd = ::dup(seat->keymap_fd_);
        if (fd >= 0) {
            wl_keyboard_send_keymap(resource, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd,
                                    static_cast<std::uint32_t>(seat->keymap_bytes_));
        }
    }
}

void Seat::Wiring::seat_get_touch(wl_client* client, wl_resource* seat_resource,
                                  std::uint32_t id) noexcept {
    // No touch capability is advertised, and a touch object is still
    // answered: quiet, inert, the same leniency the C1 heart drew.
    wl_resource* resource =
        wl_resource_create(client, &wl_touch_interface, wl_resource_get_version(seat_resource), id);
    if (resource == nullptr) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &kTouchTable, nullptr, nullptr);
}

void Seat::Wiring::seat_release(wl_client* /*client*/, wl_resource* seat_resource) noexcept {
    wl_resource_destroy(seat_resource);
}

std::uint32_t evdev_keycode(std::uint32_t desk_code) noexcept {
    // The visor named arrows live at 0x110-0x113 by convention; evdev
    // holds the same arrows at 103-108, and the wire speaks evdev.
    switch (desk_code) {
        case 0x110: // visor kKeyUp
            return 103;
        case 0x111: // visor kKeyDown
            return 108;
        case 0x112: // visor kKeyLeft
            return 105;
        case 0x113: // visor kKeyRight
            return 106;
        default:
            return desk_code;
    }
}

void bind_seat(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) noexcept {
    Seat::Wiring::bind(client, data, version, id);
}

} // namespace vesper::compositor
