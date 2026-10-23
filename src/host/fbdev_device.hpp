#pragma once

#include <cstddef>
#include <cstdint>

#include "visor/base/geometry/rect.hpp"

namespace vesper::host {

/**
 * @brief Owning handle on a Linux framebuffer (/dev/fb0), filled from mono.
 *
 * The device half of the fbdev lane: open() maps the framebuffer and reads
 * its geometry, expand() paints one mono region into it. The pixel journey
 * is the core's own Cinux shape -- a one-bit canvas expanded on the host
 * side -- so the expansion borrows visor's cinux binding (frame_expand)
 * rather than growing a second packing formula here.
 *
 * XRGB8888 (32bpp) is the supported layout today, the one QEMU's vga std
 * gives the bare guest; other layouts leave ready() false, the same honest
 * refusal as an empty table entry. The 16bpp (RGB565) lane waits on the
 * color core growing in visor upstream. A color desk talks to the same
 * frame through blit() (XRGB8888 regions, word for word), keeping expand()
 * for mono canvases.
 *
 * Borrow note: adapted from Cinux-GUI@0b0c135 host/fbdev_device.{hpp,cpp}
 * (2026-10-20) -- the ioctl/mmap skeleton and the 32bpp-only stance are
 * that repo's; blit_rect's XRGB8888 memcpy became a mono expand through
 * visor's cinux binding, so this repo carries no second packing formula.
 */
class FbdevDevice {
  public:
    FbdevDevice() = default;
    ~FbdevDevice();

    FbdevDevice(const FbdevDevice&) = delete;
    FbdevDevice& operator=(const FbdevDevice&) = delete;

    /// Opens and maps @p path (e.g. "/dev/fb0"). False on any failure,
    /// including a pixel layout this device does not expand into.
    bool open(const char* path) noexcept;

    /// Unmaps and closes. Idempotent.
    void close() noexcept;

    [[nodiscard]] bool ready() const noexcept { return map_ != nullptr; }
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] std::uint32_t stride_bytes() const noexcept { return stride_; }

    /// Expands one mono region (canvas packing: row-major, MSB first) into
    /// the mapped frame, @p on and @p off picking which XRGB8888 word each
    /// bit paints. Parts of @p rect outside the frame clip silently.
    void expand(visor::base::RectI rect, const std::uint8_t* mono, std::size_t mono_stride_bytes,
                std::uint32_t on, std::uint32_t off) noexcept;

    /// Copies one XRGB8888 region (four bytes per pixel, region-stride
    /// rows) into the mapped frame, respecting the frame's own row pitch.
    /// Parts of @p rect outside the frame clip silently.
    void blit(visor::base::RectI rect, const std::uint8_t* pixels,
              std::size_t stride_bytes) noexcept;

  private:
    int fd_ = -1;
    void* map_ = nullptr;
    std::size_t map_len_ = 0;
    int width_ = 0;
    int height_ = 0;
    std::uint32_t stride_ = 0; // framebuffer row pitch (bytes)
};

} // namespace vesper::host
