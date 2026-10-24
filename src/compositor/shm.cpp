#include "compositor/shm.hpp"

#include <cstddef>
#include <cstdint>
#include <new>

#include <sys/mman.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

namespace vesper::compositor {
namespace {

// The one format pair the desk's canvas speaks: an XRGB word is the same
// memory with the high byte promised unused, an ARGB word carries an alpha
// the canvas folds away. Everything else a guest offers is refused by name.
constexpr std::uint32_t kFormatArgb = WL_SHM_FORMAT_ARGB8888;
constexpr std::uint32_t kFormatXrgb = WL_SHM_FORMAT_XRGB8888;

ShmPool* pool_from_resource(wl_resource* resource) noexcept {
    return static_cast<ShmPool*>(wl_resource_get_user_data(resource));
}

void buffer_destroy(wl_client* /*client*/, wl_resource* resource) noexcept {
    wl_resource_destroy(resource);
}

void buffer_destroyed(wl_resource* resource) noexcept {
    auto* buffer = static_cast<ShmBuffer*>(wl_resource_get_user_data(resource));
    buffer->resource = nullptr;
    buffer->unref();
}

void pool_resize(wl_client* /*client*/, wl_resource* pool_resource, std::int32_t size) noexcept {
    ShmPool* pool = pool_from_resource(pool_resource);
    if (size <= 0 || !pool->resize(static_cast<std::size_t>(size))) {
        // The spec gives a refused resize no word of its own; an
        // implementation error ends the guest's misuse honestly instead of
        // leaving the two sides disagreeing about the mapping's size.
        wl_resource_post_error(pool_resource, WL_DISPLAY_ERROR_IMPLEMENTATION,
                               "shm pool resize refused");
    }
}

void pool_destroy(wl_client* /*client*/, wl_resource* pool_resource) noexcept {
    wl_resource_destroy(pool_resource);
}

// The pool resource's own goodbye: an unref, never a tear-down -- buffers
// still standing on the mapping keep it alive, and each takes their own
// unref when they go.
void pool_destroyed(wl_resource* pool_resource) noexcept {
    pool_from_resource(pool_resource)->unref();
}

const struct wl_buffer_interface kBufferTable = {
    .destroy = buffer_destroy,
};

void pool_create_buffer(wl_client* client, wl_resource* pool_resource, std::uint32_t id,
                        std::int32_t offset, std::int32_t width, std::int32_t height,
                        std::int32_t stride, std::uint32_t format) noexcept {
    ShmPool* pool = pool_from_resource(pool_resource);
    if (format != kFormatArgb && format != kFormatXrgb) {
        wl_resource_post_error(pool_resource, WL_SHM_ERROR_INVALID_FORMAT,
                               "format %u is not one the desk reads", format);
        return;
    }
    // The shape checks run in the wide types: a stride or a height big
    // enough to overflow int math is exactly the guest this catches.
    if (width <= 0 || height <= 0 || stride < 0 || offset < 0) {
        wl_resource_post_error(pool_resource, WL_SHM_ERROR_INVALID_STRIDE,
                               "buffer shape is not a rectangle");
        return;
    }
    const ShmPool* const_owner = pool;
    BufferView view = const_owner->view(static_cast<std::size_t>(offset), width, height,
                                        static_cast<std::size_t>(stride));
    if (view.words == nullptr) {
        // Either the geometry refused (a stride under one row of pixels is
        // no stride) or the buffer leaves the pool's mapping.
        wl_resource_post_error(pool_resource, WL_SHM_ERROR_INVALID_STRIDE,
                               "buffer does not fit its pool");
        return;
    }
    view.has_alpha = format == kFormatArgb;
    auto* buffer = new (std::nothrow) ShmBuffer;
    if (buffer == nullptr) {
        wl_client_post_no_memory(client);
        return;
    }
    buffer->pool = pool;
    buffer->view = view;
    pool->ref();
    wl_resource* resource = wl_resource_create(client, &wl_buffer_interface,
                                               wl_resource_get_version(pool_resource), id);
    if (resource == nullptr) {
        pool->unref();
        delete buffer;
        wl_client_post_no_memory(client);
        return;
    }
    buffer->resource = resource;
    wl_resource_set_implementation(resource, &kBufferTable, buffer, &buffer_destroyed);
}

const struct wl_shm_pool_interface kPoolTable = {
    .create_buffer = pool_create_buffer,
    .destroy = pool_destroy,
    .resize = pool_resize,
};

void shm_create_pool(wl_client* client, wl_resource* shm_resource, std::uint32_t id,
                     std::int32_t fd, std::int32_t size) noexcept {
    if (fd < 0 || size <= 0) {
        if (fd >= 0) {
            ::close(fd);
        }
        wl_resource_post_error(shm_resource, WL_SHM_ERROR_INVALID_FD,
                               "a pool needs a real fd and a size");
        return;
    }
    ShmPool* pool = ShmPool::adopt(fd, static_cast<std::size_t>(size));
    if (pool == nullptr) {
        // adopt closed the fd either way; an unmappable fd is the spec's
        // own fd error.
        wl_resource_post_error(shm_resource, WL_SHM_ERROR_INVALID_FD, "mmap refused the pool");
        return;
    }
    wl_resource* resource = wl_resource_create(client, &wl_shm_pool_interface,
                                               wl_resource_get_version(shm_resource), id);
    if (resource == nullptr) {
        pool->unref();
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &kPoolTable, pool, &pool_destroyed);
}

const struct wl_shm_interface kShmTable = {
    .create_pool = shm_create_pool,
};

} // namespace

void ShmBuffer::ref() noexcept {
    ++refs;
}

void ShmBuffer::unref() noexcept {
    if (--refs == 0) {
        pool->unref();
        delete this;
    }
}

ShmPool* ShmPool::adopt(int fd, std::size_t bytes) noexcept {
    // The server only ever reads the pool (writes are the guest's half of
    // the shared mapping), and the fd dies here: the mapping is the
    // relationship now.
    void* map = ::mmap(nullptr, bytes, PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);
    if (map == MAP_FAILED) {
        return nullptr;
    }
    auto* pool = new (std::nothrow) ShmPool;
    if (pool == nullptr) {
        ::munmap(map, bytes);
        return nullptr;
    }
    pool->base_ = map;
    pool->bytes_ = bytes;
    pool->refs_ = 1; // the pool resource's own hold
    return pool;
}

void ShmPool::ref() noexcept {
    ++refs_;
}

void ShmPool::unref() noexcept {
    if (--refs_ == 0) {
        ::munmap(base_, bytes_);
        delete this;
    }
}

void* ShmPool::base() const noexcept {
    return base_;
}

std::size_t ShmPool::bytes() const noexcept {
    return bytes_;
}

bool ShmPool::resize(std::size_t bytes) noexcept {
    void* moved = ::mremap(base_, bytes_, bytes, MREMAP_MAYMOVE);
    if (moved == MAP_FAILED) {
        return false;
    }
    base_ = moved;
    bytes_ = bytes;
    return true;
}

BufferView ShmPool::view(std::size_t offset, int width, int height,
                         std::size_t stride_bytes) const noexcept {
    BufferView out{};
    const std::size_t width_bytes = static_cast<std::size_t>(width) * 4U;
    if (width_bytes == 0 || stride_bytes < width_bytes || height <= 0) {
        return out; // a stride under one pixel row is no stride at all
    }
    const std::size_t last_row = offset + ((static_cast<std::size_t>(height) - 1U) * stride_bytes);
    if (last_row > bytes_ || width_bytes > bytes_ - last_row) {
        return out; // the last row leaves the mapping; the size_t math kept
                    // the addition from wrapping on the way here
    }
    out.words =
        reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(base_) + offset);
    out.width = width;
    out.height = height;
    out.stride_words = stride_bytes / 4U;
    return out;
}

void bind_shm(wl_client* client, void* /*data*/, std::uint32_t version, std::uint32_t id) noexcept {
    wl_resource* resource =
        wl_resource_create(client, &wl_shm_interface, static_cast<int>(version), id);
    if (resource == nullptr) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &kShmTable, nullptr, nullptr);
    // The format words are the pool's menu; the desk reads exactly these
    // two, in the order every compositor sends them.
    wl_shm_send_format(resource, kFormatArgb);
    wl_shm_send_format(resource, kFormatXrgb);
}

ShmBuffer* shm_buffer_from_resource(wl_resource* resource) noexcept {
    if (resource == nullptr ||
        wl_resource_instance_of(resource, &wl_buffer_interface, &kBufferTable) == 0) {
        return nullptr;
    }
    return static_cast<ShmBuffer*>(wl_resource_get_user_data(resource));
}

} // namespace vesper::compositor
