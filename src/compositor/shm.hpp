#pragma once

#include <cstddef>
#include <cstdint>

// The library's C names keep their own shape here; the house style has no
// word to say about a borrowed spelling.
struct wl_client;   // NOLINT(readability-identifier-naming)
struct wl_resource; // NOLINT(readability-identifier-naming)

namespace vesper::compositor {

/// One shared-memory buffer's read view: where its words live and what shape
/// they stand in. A view is borrowed, never owned -- the mapping belongs to
/// the pool below, and the pool stays mapped as long as any buffer holds it.
struct BufferView {
    const std::uint32_t* words = nullptr;
    int width = 0;
    int height = 0;
    std::size_t stride_words = 0;
    bool has_alpha = false; // ARGB8888 words against XRGB8888 padding bytes
};

/// One wl_shm_pool: a mapping whose bytes the client owns and the server
/// reads through its own copy of the fd. Buffers reference the pool, so the
/// mapping outlives the pool's own protocol object whenever buffers still
/// stand on it -- the pool's destroy is an unref, never a tear-down.
class ShmPool {
  public:
    /// Maps size bytes of the fd and closes it; null when the mapping
    /// refuses (the fd is closed either way).
    [[nodiscard]] static ShmPool* adopt(int fd, std::size_t bytes) noexcept;

    void ref() noexcept;
    /// Drops one holder; the last one unmaps and frees the pool.
    void unref() noexcept;

    [[nodiscard]] void* base() const noexcept;
    [[nodiscard]] std::size_t bytes() const noexcept;

    /// Grows or shrinks the mapping in place (the kernel may move it).
    /// False when the remap refuses, the old mapping untouched.
    [[nodiscard]] bool resize(std::size_t bytes) noexcept;

    /// The buffer at offset: bounds-checked here so the request handler can
    /// turn a refusal into the protocol error it names. A refused view is
    /// all-zero.
    [[nodiscard]] BufferView view(std::size_t offset, int width, int height,
                                  std::size_t stride_bytes) const noexcept;

  private:
    ShmPool() = default;
    ~ShmPool() = default;

    void* base_ = nullptr;
    std::size_t bytes_ = 0;
    int refs_ = 0;
};

/// One wl_buffer over a pool: the shape the words stand in plus the pool it
/// borrows its mapping from. The wl_buffer resource's user data from its
/// create_buffer to its destroy. Surface references retain the pixels after
/// the client destroys the wl_buffer; only a live resource receives release.
struct ShmBuffer {
    ShmPool* pool = nullptr;
    BufferView view{};
    wl_resource* resource = nullptr;
    int refs = 1;

    void ref() noexcept;
    void unref() noexcept;
};

/// The wl_shm global's bind door and the request tables that stand behind
/// it; the server wires this into wl_global_create.
void bind_shm(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) noexcept;

/// The ShmBuffer behind a wl_buffer resource, or null when the resource is
/// not a buffer of ours -- the surface chapter's guard before it reads.
[[nodiscard]] ShmBuffer* shm_buffer_from_resource(wl_resource* resource) noexcept;

} // namespace vesper::compositor
