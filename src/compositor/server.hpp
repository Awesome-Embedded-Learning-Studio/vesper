#pragma once

#include <cstdint>

#include "compositor/seat.hpp"
#include "compositor/shell.hpp"
#include "compositor/surface.hpp"

struct wl_client;  // NOLINT(readability-identifier-naming)
struct wl_display; // NOLINT(readability-identifier-naming)

namespace vesper::compositor {

/**
 * @brief The compositor heart (the roadmap's C1): one wl_display with its
 *        event loop and the globals a guest binds first -- wl_compositor,
 *        wl_shm, wl_seat, xdg_wm_base and wl_output.
 *
 * The desk's loop stays the only loop: the server owns no thread and no
 * clock of its own, it hands the event loop's fd to the caller's poll() the
 * same way the evdev nodes and the pty master hand theirs, and every advance
 * is one dispatch() call the desk makes between its rounds. Guests arrive
 * through adopt_client (the judgment lane passes a socketpair half) or
 * through the published socket (the eyeball lane); both are the same
 * clients to the same globals.
 *
 * The heart's interfaces grow with the series: each chapter wires its own
 * request tables in, and a guest that calls a request whose chapter has not
 * landed yet hears libwayland's unknown-method error, the honest answer
 * from a half-built server.
 */
class Server {
  public:
    Server() noexcept;
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /// The extent guests hear about through the one output global: the
    /// desk reports its canvas size before the bridge launches, because
    /// Xwayland needs an output before its X screen stands up.
    void note_output_extent(int width, int height) noexcept;

    /// Whether the display and its globals came up. An invalid server
    /// answers every door honestly: no fd, no dispatch, no clients.
    [[nodiscard]] bool valid() const noexcept;

    /// The event loop's pollable fd, or -1 on an invalid server.
    [[nodiscard]] int event_fd() const noexcept;

    /// Serves whatever has arrived without waiting past timeout_ms. False is
    /// the dispatch itself failing (a dying loop), not a quiet round.
    [[nodiscard]] bool dispatch(int timeout_ms) noexcept;

    /// Pushes every client's pending events out. The desk calls it after the
    /// round that produced the events, the same line the pty pump ends on.
    void flush_clients() noexcept;

    /// The desk's presentation word, once per rendered round: frame
    /// callbacks hear their done words, consumed buffers their release, and
    /// the flush carries both. A desk that never renders never fires -- the
    /// cadence is the desk's own.
    void end_frame() noexcept;

    /// Adopts an already-connected socket as a client: the judgment lane
    /// passes a socketpair half, the spawn lane passes a child's channel.
    /// The fd becomes the client's own on success and is closed on refusal;
    /// the client it became is the spawn lanes' word to keep.
    [[nodiscard]] wl_client* adopt_client(int fd) noexcept;

    /// Publishes the display on a socket in XDG_RUNTIME_DIR and returns its
    /// name, or nullptr when it could not be published. The eyeball lane's
    /// door; the headless lane never needs it.
    [[nodiscard]] const char* publish_socket() noexcept;

    /// Live clients, connections and disconnections counted as they happen.
    [[nodiscard]] int client_count() const noexcept;

    /// Live surfaces, in creation order: what the desk can composite.
    [[nodiscard]] int surface_count() const noexcept;

    /// One surface's current committed image (null words when it holds
    /// nothing); an empty damage rect means "the whole surface", the
    /// conservative read when the guest reported nothing narrower.
    [[nodiscard]] Surface::Frame surface_frame(int index) const noexcept;

    /// The time stamp the next flush stamps onto frame callbacks; the desk
    /// sets it from its own clock each round (the cadence chapter wires the
    /// real clock, zero is still a valid answer).
    void set_frame_time_ms(std::uint32_t ms) noexcept;

    /// Live shell surfaces (guest windows), in arrival order; the desk
    /// hosts the faces these name.
    [[nodiscard]] int shell_surface_count() const noexcept;
    [[nodiscard]] ShellSurface* shell_surface(int index) const noexcept;

    /// Associates an X window with this client's wl_surface, or waits for
    /// the Wayland creation request to arrive on its independent stream.
    [[nodiscard]] ShellSurface* adopt_xwindow(wl_client* client, std::uint32_t surface_id,
                                              const char* title) noexcept;

    /// Wires the map and unmap notices; the desk's window layer listens
    /// here (a null callback is a quiet desk, never a stuck one).
    void set_shell_hooks(const ShellHooks& hooks) noexcept;

    /// The desk's one seat: input words leave through it. An invalid
    /// server hands back a quiet empty one.
    [[nodiscard]] Seat& seat() noexcept;

    /// The desk's clock word for the round: keys, motions, wheels and the
    /// frame callbacks' done stamps all carry it.
    void note_time_ms(std::uint32_t ms) noexcept;

  private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace vesper::compositor
