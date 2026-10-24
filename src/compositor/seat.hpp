#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "compositor/surface.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/bindings/events/modifiers.hpp"

struct wl_client;   // NOLINT(readability-identifier-naming)
struct wl_resource; // NOLINT(readability-identifier-naming)

namespace vesper::compositor {

class Surface;

/**
 * @brief The desk's one seat (the roadmap's C2): keyboard and pointer words
 *        for whichever guest holds the focus.
 *
 * The seat keeps one binding per client that bound it (the seat resource
 * itself plus whatever pointer and keyboard objects that client asked for)
 * and sends every word to the focused surface's client only. Focus is the
 * desk's decision, handed in through pointer_enter and keyboard_enter; the
 * seat never guesses it. Key codes and button codes pass through in Linux
 * native form -- the one translation is the desk's own named arrows, which
 * the visor convention deliberately parks outside evdev's numbers and
 * which the wire expects back inside them.
 *
 * The clock word rides beside the events: the desk notes its time once per
 * round and every key, motion and wheel word carries it.
 */
class Seat {
    struct Binding; // the per-client state, spelled in full below

  public:
    static constexpr std::size_t kClientCapacity = 16;

    Seat() noexcept;
    ~Seat();

    Seat(const Seat&) = delete;
    Seat& operator=(const Seat&) = delete;

    /// The desk's clock word for this round; keys, motions and wheels
    /// carry it verbatim.
    void note_time_ms(std::uint32_t ms) noexcept;

    // --- the pointer, in surface-local words --------------------------------

    /// Pointer focus crosses to a surface (null leaves nobody); the guest
    /// hears enter or leave with the position it stands at.
    void pointer_enter(Surface* surface, visor::base::PointI at) noexcept;
    void pointer_leave() noexcept;

    /// A motion while the focus holds; surface-local coordinates.
    void pointer_motion(visor::base::PointI at) noexcept;

    /// A button word; the code passes through in Linux native form.
    void pointer_button(std::uint32_t button, bool pressed) noexcept;

    /// Wheel detents; each notch travels as 120 of the pointer's
    /// high-resolution word, the conservative start the roadmap names.
    void pointer_wheel(std::int32_t dx, std::int32_t dy) noexcept;

    /// The surface under the pointer, if any.
    [[nodiscard]] Surface* pointer_focus() const noexcept;

    // --- the keyboard -------------------------------------------------------

    /// Keyboard focus crosses to a surface (null leaves nobody).
    void keyboard_enter(Surface* surface) noexcept;
    void keyboard_leave() noexcept;

    /// A key word; the code arrives in Linux native form, the desk's named
    /// arrows folded back into evdev's numbers on the way.
    void key(std::uint32_t code, bool pressed) noexcept;

    /// The modifier set the desk holds; the guest hears the xkb masks.
    void set_modifiers(visor::host::Modifiers modifiers) noexcept;

    /// The surface holding the keyboard, if any.
    [[nodiscard]] Surface* keyboard_focus() const noexcept;

    // The protocol tables' hands, the Wiring shape again: every request
    // and every resource-goodbye that reaches this seat's state.
    struct Wiring {
        /// The binding a seat-side resource belongs to; the goodbye hands
        /// find their slot through it.
        [[nodiscard]] static Binding* binding_of_resource(Seat* seat,
                                                          wl_resource* resource) noexcept;
        static void bind(wl_client* client, void* data, std::uint32_t version,
                         std::uint32_t id) noexcept;
        static void seat_get_pointer(wl_client* client, wl_resource* seat,
                                     std::uint32_t id) noexcept;
        static void seat_get_keyboard(wl_client* client, wl_resource* seat,
                                      std::uint32_t id) noexcept;
        static void seat_get_touch(wl_client* client, wl_resource* seat, std::uint32_t id) noexcept;
        static void seat_release(wl_client* client, wl_resource* seat) noexcept;
        static void pointer_destroyed(wl_resource* resource) noexcept;
        static void keyboard_destroyed(wl_resource* resource) noexcept;
        static void seat_destroyed(wl_resource* resource) noexcept;
    };

  private:
    friend struct Wiring;

    /// One client's seat belongings.
    struct Binding {
        wl_client* client = nullptr;
        wl_resource* seat = nullptr;
        wl_resource* pointer = nullptr;
        wl_resource* keyboard = nullptr;
    };

    [[nodiscard]] Binding* binding_of_client(wl_client* client) noexcept;
    [[nodiscard]] Binding* binding_of_surface(Surface* surface) noexcept;
    [[nodiscard]] Binding* pointer_binding() noexcept;
    [[nodiscard]] Binding* keyboard_binding() noexcept;
    [[nodiscard]] std::uint32_t next_serial() noexcept;

    std::array<Binding, kClientCapacity> bindings_{};
    Surface* pointer_focus_ = nullptr;
    Surface* keyboard_focus_ = nullptr;
    std::uint32_t time_ms_ = 0;
    std::uint32_t serial_ = 0;
    int keymap_fd_ = -1;
    std::size_t keymap_bytes_ = 0;
};

/// The wl_seat global's bind door and the request tables behind it.
void bind_seat(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) noexcept;

/// The evdev number a desk key code stands for: the visor convention parks
/// its named arrows outside evdev's printable band, and the wire wants them
/// back inside it; everything else passes through untouched.
[[nodiscard]] std::uint32_t evdev_keycode(std::uint32_t desk_code) noexcept;

} // namespace vesper::compositor
