#pragma once

#include "compositor/server.hpp"
#include "visor/bindings/host.hpp"

#include <span>
#include <sys/types.h>

struct wl_client;   // NOLINT(readability-identifier-naming)
struct wl_listener; // NOLINT(readability-identifier-naming)

namespace vesper::compositor {

/**
 * @brief One guest program birthed through the desk table's fourth slot
 *        (the roadmap's T3): a Wayland client whose connection is a
 *        socketpair the desk owns.
 *
 * The birth shape the XWayland bridge pioneered, lifted to any program: the
 * child's guest connection rides at its own fd number through
 * WAYLAND_SOCKET (the pass-as-is gift shape), so nothing under
 * XDG_RUNTIME_DIR is needed and the judgment lane births real binaries the
 * same door a spawned guest of the eyeball desk would arrive through.
 * Everything else about the program is the caller's words -- path and argv,
 * exec conventions intact. The slot only births, never waits: the reap is
 * this object's own, lazily, on alive().
 */
class GuestSpawn {
  public:
    GuestSpawn() noexcept;
    ~GuestSpawn();

    GuestSpawn(const GuestSpawn&) = delete;
    GuestSpawn& operator=(const GuestSpawn&) = delete;

    /// Births the program and adopts its connection. False (with the
    /// cleanup already done) when the table cannot birth, the program is
    /// missing, or the server refuses the adoption. A guest that has left
    /// (its connection dropped, its child reaped) may be birthed again --
    /// one live connection at a time. The server must outlive the guest.
    [[nodiscard]] bool launch(Server& server, const visor::host::HostTable& table, const char* path,
                              std::span<const char* const> argv) noexcept;

    /// Whether the child still lives. A reaped child reads as dead, and the
    /// reap itself happens in here, so a dead guest leaves no zombie
    /// behind.
    [[nodiscard]] bool alive() const noexcept;

    /// The adopted connection, null before a launch and after the guest
    /// hung up (a client that leaves does not take the object with it).
    [[nodiscard]] wl_client* client() const noexcept;

  private:
    /// The destroy notice's own storage: a wl_listener plus the owner it
    /// hands the hang-up to, defined in the .cpp beside its one use.
    struct DestroyNotice;

    static void client_gone(wl_listener* listener, void* data) noexcept;
    void shutdown_child() noexcept;

    wl_client* client_ = nullptr;
    DestroyNotice* notice_ = nullptr;
    /// The lazy reap in alive() is bookkeeping, not a state any reader can
    /// observe, so the word bends for the const read.
    mutable pid_t child_ = -1;
};

} // namespace vesper::compositor
