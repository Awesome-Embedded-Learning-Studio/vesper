#pragma once

#include "visor/bindings/host.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

struct wl_client; // NOLINT(readability-identifier-naming)

namespace vesper::compositor {

class Server;
class ShellSurface;

/**
 * @brief The XWayland bridge (the roadmap's C5): one real Xwayland child
 *        process, its window-manager wire, and the desk-side glue.
 *
 * Rootless Xwayland gives each X window a plain wl_surface, associated
 * through the WL_SURFACE_ID client message. The XWM redirects the X
 * windows through Composite and attaches desk-side shell roles without
 * creating xdg protocol objects or X-side frame windows. Close requests
 * become WM_DELETE_WINDOW for clients that support it, or a client kill.
 *
 * The bridge supplies an abstract X listener and waits for displayfd
 * readiness while serving Wayland. The XWM wire uses the system xcb and
 * xcb-composite libraries.
 */
class XWayland {
  public:
    static constexpr std::size_t kFaceCapacity = 16;

    XWayland() noexcept;
    ~XWayland();

    XWayland(const XWayland&) = delete;
    XWayland& operator=(const XWayland&) = delete;

    /// Spawns Xwayland wired to this server, birthed through the desk
    /// table's fourth slot: its Wayland half is an adopted client, its -wm
    /// half is ours, and displayfd reports readiness on a pipe while this
    /// server keeps dispatching and flushing. False (with everything
    /// cleaned up) when the table cannot birth, the binary is missing, or
    /// any leg of the wiring refuses. The server must outlive the bridge.
    [[nodiscard]] bool launch(Server& server, const visor::host::HostTable& table) noexcept;

    /// The wm wire's pollable fd, -1 before a launch; the desk's loop
    /// watches it the way it watches the server's own.
    [[nodiscard]] int event_fd() const noexcept;

    /// Serves whatever the wm wire carries without waiting.
    void pump() noexcept;

    /// Whether the child still lives (a reaped child reads as dead).
    [[nodiscard]] bool alive() const noexcept;

    /// The display assigned to the child, ":N", or empty until it settles.
    [[nodiscard]] const char* display() const noexcept;

    /// Whether a shell face belongs to an X window of this bridge.
    [[nodiscard]] bool owns(const ShellSurface* shell) const noexcept;

    /// The desk's close word for an X face: WM_DELETE_WINDOW for clients
    /// that speak it, a kill for the rest. A face the bridge does not own
    /// is not the bridge's to close.
    void request_close(ShellSurface& shell) noexcept;

  private:
    struct Impl;
    Impl* impl_ = nullptr;

    // The launch and the X bookkeeping, all in the .cpp.
    void adopt_face(std::uint32_t window, std::uint32_t surface_id) noexcept;
    void drop_face(std::uint32_t window) noexcept;
    void shutdown_child() noexcept;
    [[nodiscard]] std::uint32_t intern(const char* name) noexcept;
};

} // namespace vesper::compositor
