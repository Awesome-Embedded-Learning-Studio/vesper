#include "host/fake_session_host.hpp"

#include "visor/base/bit_ops/bitmask.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/display.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/host.hpp"
#include "visor/canvas/pixel_traits.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace vesper::host {

using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
using visor::host::Event;
using visor::host::FlushRegion;
using visor::host::HostInfo;
using visor::host::HostTable;
using visor::host::PixelFormat;

FakeSessionHost::FakeSessionHost(int width, int height)
    : width_(width), height_(height),
      frame_(((static_cast<std::size_t>(width) + 7) / 8) * static_cast<std::size_t>(height), 0) {}

HostTable FakeSessionHost::table() noexcept {
    return HostTable{
        .flush = &FakeSessionHost::flush_thunk,
        .poll_event = &FakeSessionHost::poll_event_thunk,
        .clock_ms = &FakeSessionHost::clock_thunk,
        .context = this,
        .info =
            HostInfo{
                .screen = SizeI{.w = width_, .h = height_},
                .format = PixelFormat::Mono1,
            },
    };
}

void FakeSessionHost::flush_thunk(void* context, const FlushRegion& region) noexcept {
    static_cast<FakeSessionHost*>(context)->handle_flush(region);
}

bool FakeSessionHost::poll_event_thunk(void* context, Event& out) noexcept {
    return static_cast<FakeSessionHost*>(context)->feed_.pop(out);
}

std::uint64_t FakeSessionHost::clock_thunk(void* context) noexcept {
    return static_cast<FakeSessionHost*>(context)->now_ms_;
}

void FakeSessionHost::handle_flush(const FlushRegion& region) noexcept {
    // The offscreen host's refusal line: any in-bounds region of the right
    // format lands, everything else is refused and counted, because both a
    // wrong format and a wrong rectangle mean a sender that computed its
    // window wrong. The region log records accepted arrivals only.
    const bool acceptable = region.format == PixelFormat::Mono1 && region.pixels != nullptr &&
                            !region.rect.is_empty() && region.rect.pos.x >= 0 &&
                            region.rect.pos.y >= 0 &&
                            region.rect.pos.x + region.rect.size.w <= width_ &&
                            region.rect.pos.y + region.rect.size.h <= height_;
    if (!acceptable) {
        ++rejected_count_;
        return;
    }
    // The log is judgment data: its growth is bounded by the test that runs,
    // and an allocation failure here is a dead test process either way.
    regions_.push_back(region.rect);
    blit(region);
    ++flush_count_;
}

void FakeSessionHost::blit(const FlushRegion& region) noexcept {
    const std::size_t stride = (static_cast<std::size_t>(width_) + 7) / 8;
    for (int row = 0; row < region.rect.size.h; ++row) {
        const std::uint8_t* src_row =
            region.pixels + (static_cast<std::size_t>(row) * region.stride_bytes);
        const std::size_t dst_base =
            (static_cast<std::size_t>(region.rect.pos.y) + static_cast<std::size_t>(row)) * stride;
        for (int col = 0; col < region.rect.size.w; ++col) {
            const auto src_x = static_cast<std::size_t>(col);
            const auto dst_x = static_cast<std::size_t>(region.rect.pos.x) + src_x;
            const visor::base::bit::BitMask<std::uint8_t> src_word{src_row[src_x / 8]};
            const bool lit = src_word.has(
                visor::base::bit::mask_bit<std::uint8_t>(visor::canvas::pixel_bit_position(src_x)));
            const std::size_t dst_index = dst_base + (dst_x / 8);
            visor::base::bit::BitMask<std::uint8_t> dst_word{frame_[dst_index]};
            dst_word.deposit(visor::base::bit::bit_at(visor::canvas::pixel_bit_position(dst_x)),
                             lit ? 1 : 0);
            frame_[dst_index] = dst_word.raw;
        }
    }
}

bool FakeSessionHost::get(PointI p) const noexcept {
    if (p.x < 0 || p.y < 0 || p.x >= width_ || p.y >= height_) {
        return false;
    }
    const auto x = static_cast<std::size_t>(p.x);
    const auto y = static_cast<std::size_t>(p.y);
    const std::size_t stride = (static_cast<std::size_t>(width_) + 7) / 8;
    const visor::base::bit::BitMask<std::uint8_t> word{frame_[(y * stride) + (x / 8)]};
    return word.has(visor::base::bit::mask_bit<std::uint8_t>(visor::canvas::pixel_bit_position(x)));
}

const std::vector<RectI>& FakeSessionHost::regions() const noexcept {
    return regions_;
}

void FakeSessionHost::clear_regions() noexcept {
    regions_.clear();
}

int FakeSessionHost::flush_count() const noexcept {
    return flush_count_;
}

int FakeSessionHost::rejected_count() const noexcept {
    return rejected_count_;
}

bool FakeSessionHost::push_event(const Event& event) noexcept {
    if (feed_.push(event)) {
        return true;
    }
    ++dropped_event_count_;
    return false;
}

int FakeSessionHost::dropped_event_count() const noexcept {
    return dropped_event_count_;
}

void FakeSessionHost::advance_ms(std::uint64_t delta) noexcept {
    now_ms_ += delta;
}

} // namespace vesper::host
