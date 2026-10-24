#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "compositor/shm.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"

struct wl_client;
struct wl_resource; // NOLINT(readability-identifier-naming)

namespace vesper::compositor {

class Surface;

/// The server's book of live surfaces, the one registry the request
/// handlers and the desk both read: handlers add and strike surfaces as
/// guests create and destroy them, the desk composites what the book holds.
/// A fixed table, because a teaching desk hosts a teaching handful of
/// guests; a guest over the cap gets its surface refused with a no-memory
/// word, the honest small-server answer.
class SurfaceBook {
  public:
    static constexpr std::size_t kCapacity = 16;

    /// Adds one surface; false means the table is full.
    [[nodiscard]] bool add(Surface* surface) noexcept;

    /// Strikes one surface; a surface not in the book is a no-op, so a
    /// destroy racing a full table stays quiet.
    void remove(Surface* surface) noexcept;

    [[nodiscard]] std::size_t count() const noexcept { return count_; }
    [[nodiscard]] Surface* at(std::size_t index) const noexcept;

  private:
    std::array<Surface*, kCapacity> slots_{};
    std::size_t count_ = 0;
};

/**
 * @brief One wl_surface: a guest's canvas seat, double-buffered by the
 *        protocol's own shape.
 *
 * Attach, damage and frame land on the pending state; commit flips the
 * pending buffer into current, sends the retired buffer home with its
 * release word, and stamps the epoch the desk's invalidation reads. The
 * desk-side view is borrowed: it points into the shm mapping and stays
 * valid until the guest's next commit retires it, which in this
 * single-threaded desk never happens mid-draw.
 *
 * Frame callbacks ride along: the request parks a callback on the pending
 * state, commit moves it to the firing list, and the server's flush -- the
 * moment the desk's own round ends -- sends the done word with the desk's
 * frame time. The cadence chapter (C4) tightens the timing; the plumbing is
 * the same plumbing.
 */
class Surface {
  public:
    /// What the desk reads after a commit: the current image and its
    /// coordinates in one snapshot value.
    struct Frame {
        BufferView view{};           // words null when nothing is held
        visor::base::RectI damage{}; // the last commit's damage bounds
        std::uint64_t epoch = 0;     // commits with a buffer since birth
    };

    /// The wl_surface resource's user data from create_surface to
    /// destroy; the book is the one it strikes itself out of.
    Surface(wl_resource* resource, SurfaceBook* book) noexcept;
    ~Surface();

    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    /// The desk's snapshot of the current state.
    [[nodiscard]] Frame frame() const noexcept;

    /// The wl_surface resource itself, for the layers that must speak to
    /// the guest about this surface (the seat's enter words name it).
    [[nodiscard]] wl_resource* resource() const noexcept { return resource_; }

    /// The desk's frame endgame, called once per presented round: the
    /// collected callbacks hear their done words, and the buffer the desk
    /// just consumed goes home with its release word -- a single-buffer
    /// guest waiting on that word can draw its next frame the moment the
    /// desk has taken its pixels, and never once deadlocks.
    void end_frame(std::uint32_t frame_ms) noexcept;

    /// Drops held callbacks and buffers without words: the surface's own
    /// destroy, or the guest's death, already ended the conversation.
    void silence() noexcept;

    // The request handlers' hooks, public the way RemoteCanvas's input
    // overrides are: the protocol table in the .cpp is the only caller, and
    // hiding them would only promise an encapsulation the wire never asked
    // for.
    void handle_attach(wl_resource* buffer) noexcept;
    void handle_damage(visor::base::RectI area) noexcept;
    void handle_frame_request(wl_client* client, std::uint32_t id) noexcept;
    void handle_commit() noexcept;

    /// The role a shell hangs on a surface: a tail on every commit and a
    /// notice on the surface's death. One role per surface -- the shell
    /// chapter's one-role rule -- so a second attach refuses, and the
    /// refuse word is the guest's to hear.
    using CommitHook = void (*)(Surface& surface, void* context) noexcept;
    using DestroyHook = void (*)(Surface& surface, void* context) noexcept;
    [[nodiscard]] bool attach_role(CommitHook on_commit, DestroyHook on_destroy,
                                   void* context) noexcept;

    /// Clears the role the context owns; a stranger's context changes
    /// nothing.
    void detach_role(const void* context) noexcept;

  private:
    wl_resource* resource_ = nullptr;
    SurfaceBook* book_ = nullptr;
    CommitHook commit_hook_ = nullptr;
    DestroyHook destroy_hook_ = nullptr;
    void* role_context_ = nullptr;

    // Pending state, gathered between commits.
    ShmBuffer* pending_buffer_ = nullptr;
    bool pending_attached_ = false;
    visor::base::RectI pending_damage_{};
    bool pending_damaged_ = false;

    // Current state, what the desk reads.
    ShmBuffer* held_buffer_ = nullptr;
    BufferView view_{};
    visor::base::RectI damage_{};
    std::uint64_t epoch_ = 0;

    // Callbacks waiting for the desk's next frame.
    static constexpr std::size_t kCallbackCapacity = 8;
    std::array<wl_resource*, kCallbackCapacity> firing_{};
    std::size_t firing_count_ = 0;
    bool held_released_ = false; // the desk consumed the held buffer already

    static void callback_gone(wl_resource* resource) noexcept;
};

/// The wl_compositor global's bind door: create_surface enters the book the
/// data pointer carries, create_region answers with an inert region (the
/// desk honors neither opaque nor input regions in C1).
/// The wl_compositor global's bind door: create_surface enters the book the
/// data pointer carries, create_region answers with an inert region (the
/// desk honors neither opaque nor input regions in C1).
void bind_compositor(wl_client* client, void* data, std::uint32_t version,
                     std::uint32_t id) noexcept;

/// The Surface behind a wl_surface resource, or null when the resource is
/// not one of ours -- the shell chapter's guard before it hangs a role.
[[nodiscard]] Surface* surface_from_resource(wl_resource* resource) noexcept;

} // namespace vesper::compositor
