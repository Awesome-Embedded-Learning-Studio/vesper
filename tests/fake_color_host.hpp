#pragma once

// The color twin of the fake session host: the same scripted session (one
// fixed screen, an event feed, a scripted clock, the region log), but the
// frame holds XRGB8888 words and only XRGB8888 regions are accepted. The
// mono desk keeps the 1bpp twin; the color-canvas judgments read whole words
// off this one, so a scheme's roles can be locked to pixels.

#include "visor/base/containers/ring_queue.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/display.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/host.hpp"
#include "visor/bindings/posix/spawn.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace vesper::host {

class FakeColorHost {
  public:
    static constexpr std::size_t kFeedCapacity = 16;

    explicit FakeColorHost(int width, int height)
        : width_{width}, height_{height},
          frame_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0U) {}

    [[nodiscard]] visor::host::HostTable table() noexcept {
        visor::host::HostTable table;
        table.flush = &FakeColorHost::flush_thunk;
        table.poll_event = &FakeColorHost::poll_event_thunk;
        table.clock_ms = &FakeColorHost::clock_thunk;
        // The fake fakes pixels, events and the clock, not births: the
        // live judgments raise real guests through this table, so the
        // fourth slot carries the real POSIX filling.
        table.spawn = &visor::host::spawn_via_posix;
        table.context = this;
        table.info = visor::host::HostInfo{
            .screen = visor::base::SizeI{.w = width_, .h = height_},
            .format = visor::host::PixelFormat::XRGB8888,
        };
        return table;
    }

    /// One received word; out-of-bounds reads as zero, the unlit canvas.
    [[nodiscard]] std::uint32_t get(visor::base::PointI p) const noexcept {
        if (p.x < 0 || p.y < 0 || p.x >= width_ || p.y >= height_) {
            return 0;
        }
        return frame_[(static_cast<std::size_t>(p.y) * static_cast<std::size_t>(width_)) +
                      static_cast<std::size_t>(p.x)];
    }

    [[nodiscard]] const std::vector<visor::base::RectI>& regions() const noexcept {
        return regions_;
    }

    void clear_regions() noexcept { regions_.clear(); }

    [[nodiscard]] int flush_count() const noexcept { return flush_count_; }

    [[nodiscard]] int rejected_count() const noexcept { return rejected_count_; }

    [[nodiscard]] bool push_event(const visor::host::Event& event) noexcept {
        return feed_.push(event);
    }

    [[nodiscard]] int dropped_event_count() const noexcept { return dropped_event_count_; }

    void advance_ms(std::uint64_t delta) noexcept { now_ms_ += delta; }

  private:
    static void flush_thunk(void* context, const visor::host::FlushRegion& region) noexcept {
        static_cast<FakeColorHost*>(context)->handle_flush(region);
    }

    static bool poll_event_thunk(void* context, visor::host::Event& out) noexcept {
        return static_cast<FakeColorHost*>(context)->feed_.pop(out);
    }

    static std::uint64_t clock_thunk(void* context) noexcept {
        return static_cast<FakeColorHost*>(context)->now_ms_;
    }

    void handle_flush(const visor::host::FlushRegion& region) noexcept {
        const bool inside = region.format == visor::host::PixelFormat::XRGB8888 &&
                            region.pixels != nullptr && !region.rect.is_empty() &&
                            region.rect.pos.x >= 0 && region.rect.pos.y >= 0 &&
                            region.rect.pos.x + region.rect.size.w <= width_ &&
                            region.rect.pos.y + region.rect.size.h <= height_;
        if (!inside) {
            ++rejected_count_;
            return;
        }
        // Whole bytes per pixel, so each row is one copy onto the word frame.
        const std::size_t row_bytes = static_cast<std::size_t>(region.rect.size.w) * 4U;
        for (int row = 0; row < region.rect.size.h; ++row) {
            const std::uint8_t* src =
                region.pixels + (static_cast<std::size_t>(row) * region.stride_bytes);
            std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(frame_.data()) +
                                (((static_cast<std::size_t>(region.rect.pos.y + row) *
                                   static_cast<std::size_t>(width_)) +
                                  static_cast<std::size_t>(region.rect.pos.x)) *
                                 4U);
            std::memcpy(dst, src, row_bytes);
        }
        regions_.push_back(region.rect);
        ++flush_count_;
    }

    int width_ = 0;
    int height_ = 0;
    int flush_count_ = 0;
    int rejected_count_ = 0;
    int dropped_event_count_ = 0;
    std::uint64_t now_ms_ = 0;
    visor::base::container::RingQueue<visor::host::Event, kFeedCapacity> feed_{};
    std::vector<visor::base::RectI> regions_;
    std::vector<std::uint32_t> frame_; // XRGB8888 words, the color canvas's twin
};

} // namespace vesper::host
