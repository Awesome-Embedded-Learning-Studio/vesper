#include "host/fbdev_device.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "visor/base/geometry/rect.hpp"
#include "visor/bindings/cinux/frame_expand.hpp"

namespace vesper::host {

FbdevDevice::~FbdevDevice() {
    close();
}

bool FbdevDevice::open(const char* path) noexcept {
    fd_ = ::open(path, O_RDWR);
    if (fd_ < 0) {
        return false;
    }

    fb_var_screeninfo vinfo{};
    fb_fix_screeninfo finfo{};
    if (::ioctl(fd_, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
        ::ioctl(fd_, FBIOGET_FSCREENINFO, &finfo) < 0) {
        close();
        return false;
    }

    if (vinfo.bits_per_pixel != 32U) {
        // One expansion path, the XRGB8888 one; anything else stays !ready.
        close();
        return false;
    }

    width_ = static_cast<int>(vinfo.xres);
    height_ = static_cast<int>(vinfo.yres);
    stride_ = finfo.line_length;
    map_len_ = finfo.smem_len;

    void* m = ::mmap(nullptr, map_len_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (m == MAP_FAILED) {
        map_ = nullptr;
        close();
        return false;
    }
    map_ = m;
    return true;
}

void FbdevDevice::close() noexcept {
    if (map_ != nullptr) {
        static_cast<void>(::munmap(map_, map_len_));
        map_ = nullptr;
    }
    map_len_ = 0;
    if (fd_ >= 0) {
        static_cast<void>(::close(fd_));
        fd_ = -1;
    }
}

void FbdevDevice::blit(visor::base::RectI rect, const std::uint8_t* pixels,
                       std::size_t stride_bytes) noexcept {
    if (map_ == nullptr || pixels == nullptr) {
        return;
    }
    const int first_col = rect.pos.x < 0 ? 0 : rect.pos.x;
    const int first_row = rect.pos.y < 0 ? 0 : rect.pos.y;
    const int last_col = rect.pos.x + rect.size.w > width_ ? width_ : rect.pos.x + rect.size.w;
    const int last_row = rect.pos.y + rect.size.h > height_ ? height_ : rect.pos.y + rect.size.h;
    for (int row = first_row; row < last_row; ++row) {
        const std::size_t src_skip = (static_cast<std::size_t>(row - rect.pos.y) * stride_bytes) +
                                     (static_cast<std::size_t>(first_col - rect.pos.x) * 4U);
        const std::size_t dst_skip =
            (static_cast<std::size_t>(row) * stride_) + (static_cast<std::size_t>(first_col) * 4U);
        const std::size_t bytes = static_cast<std::size_t>(last_col - first_col) * 4U;
        std::memcpy(static_cast<std::uint8_t*>(map_) + dst_skip, pixels + src_skip, bytes);
    }
}

void FbdevDevice::expand(visor::base::RectI rect, const std::uint8_t* mono,
                         std::size_t mono_stride_bytes, std::uint32_t on,
                         std::uint32_t off) noexcept {
    if (map_ == nullptr || mono == nullptr) {
        return;
    }
    const visor::host::cinux::FrameTarget target{
        .pixels = static_cast<std::uint32_t*>(map_),
        .width = width_,
        .height = height_,
        .pitch_px = stride_ / sizeof(std::uint32_t),
    };
    visor::host::cinux::expand_region(target, rect, mono, mono_stride_bytes, on, off);
}

} // namespace vesper::host
