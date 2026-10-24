#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "compositor/surface.hpp"

struct wl_client;   // NOLINT(readability-identifier-naming)
struct wl_resource; // NOLINT(readability-identifier-naming)

namespace vesper::compositor {

class ShellBook;
class ShellSurface;

/// What the desk hears from the shell: a toplevel mapped (configure acked,
/// buffer committed -- the guest has a face on the desk now) and the same
/// face gone (null commit, surface death or the guest's goodbye). The
/// callbacks run inside the server's dispatch, so they owe the desk
/// dispatch-speed work only.
struct ShellHooks {
    void (*on_map)(ShellSurface& shell, void* context) noexcept = nullptr;
    void (*on_unmap)(ShellSurface& shell, void* context) noexcept = nullptr;
    void* context = nullptr;
};

/**
 * @brief One xdg toplevel or X window on the desk: the desk-facing view of a
 *        guest window.
 *
 * The object is born when the guest asks for a toplevel on a surface,
 * learns its title and its first size as the guest draws them, and dies
 * with the xdg_surface (or the surface itself, whichever the guest drops
 * first -- the desk hears the same on_unmap either way). The desk reads
 * pixels through the surface the way RemoteCanvas reads them through the
 * private channel: borrowed, replayed, never owned.
 */
class ShellSurface {
  public:
    ~ShellSurface();

    /// Creates a desk window for a surface managed by the XWM. The bridge
    /// owns the returned shell and deletes it when the X window unmaps.
    [[nodiscard]] static ShellSurface* adopt_xwindow(Surface& surface, ShellBook& book,
                                                     const char* title) noexcept;

    /// The guest's title for the window, empty until set_title arrives.
    [[nodiscard]] const char* title() const noexcept;

    /// The committed size, zero while nothing is held.
    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;

    /// Whether the desk currently hosts this window's face.
    [[nodiscard]] bool mapped() const noexcept;

    /// The surface the pixels read from; null once the surface died under
    /// the shell.
    [[nodiscard]] Surface* surface() const noexcept;

    /// The desk's close: the toplevel close word, the guest's own choice
    /// how to answer.
    void request_close() noexcept;

    /// The protocol tables' hands, the same shape visor's hosts give their
    /// Wiring: every request that reaches into this object's state is a
    /// static member here, declared for the tables and defined in the
    /// .cpp beside them.
    struct Wiring {
        static void wm_base_get_xdg_surface(wl_client* client, wl_resource* wm_base,
                                            std::uint32_t id, wl_resource* surface) noexcept;
        static void xdg_surface_destroyed(wl_resource* resource) noexcept;
        static void xdg_surface_get_toplevel(wl_client* client, wl_resource* xdg_surface,
                                             std::uint32_t id) noexcept;
        static void xdg_surface_ack_configure(wl_client* client, wl_resource* resource,
                                              std::uint32_t serial) noexcept;
        static void toplevel_destroyed(wl_resource* resource) noexcept;
        static void toplevel_set_title(wl_client* client, wl_resource* resource,
                                       const char* title) noexcept;
    };

  private:
    // The commit tail and the death notice the surface's role carries.
    static void commit_notice(Surface& surface, void* context) noexcept;
    static void surface_gone(Surface& surface, void* context) noexcept;

    std::array<char, 64> title_{};
    wl_resource* xdg_surface_ = nullptr; // the surface's role object
    wl_resource* toplevel_ = nullptr;    // close words leave through here
    Surface* surface_ = nullptr;
    ShellBook* book_ = nullptr; // hooks and the configure serial live there
    bool acked_ = false;        // the guest acked a configure
    bool mapped_ = false;       // the desk hosts the face
};

/// The server's book of live shell surfaces, beside the surface book: the
/// desk walks it to lay windows, the judgments walk it to count faces. The
/// configure serial counter lives here because a configure and its ack
/// only ever meet each other.
class ShellBook {
  public:
    static constexpr std::size_t kCapacity = 16;

    [[nodiscard]] bool add(ShellSurface* shell) noexcept;
    void remove(ShellSurface* shell) noexcept;
    [[nodiscard]] std::size_t count() const noexcept { return count_; }
    [[nodiscard]] ShellSurface* at(std::size_t index) const noexcept;

    ShellHooks hooks{};

  private:
    friend struct ShellSurface::Wiring;

    std::array<ShellSurface*, kCapacity> slots_{};
    std::size_t count_ = 0;
    std::uint32_t next_serial_ = 1; // zero stays "never configured"
};

/// The xdg_wm_base global's bind door and the request tables behind it.
void bind_wm_base(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) noexcept;

} // namespace vesper::compositor
