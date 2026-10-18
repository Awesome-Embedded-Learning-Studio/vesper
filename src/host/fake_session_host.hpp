#pragma once

#include "visor/base/containers/ring_queue.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/display.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/host.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace vesper::host {

/**
 * @brief The desktop's stand-in session: one fixed screen in PC memory that
 *        records every region the desktop submits, feeds scripted events and
 *        a scripted clock, and answers pixel probes.
 *
 * visor's OffscreenHost keeps the final frame and two counters; the desktop's
 * judgments need more, because a drag must prove the old and the new frame
 * boxes were both repainted. The double therefore keeps the per-flush region
 * log beside the frame, and the log is the boundary-side witness for every
 * repaint claim a test makes.
 *
 * The shape follows the Cinux session to come: one whole screen of fixed
 * size, an in-process event queue, HostTable method calls, no terminal and
 * no PTY. What D5 swaps in is the flush target and the event source; the
 * desktop code under test never knows which session it runs against.
 */
class FakeSessionHost {
  public:
    /// Total feed capacity in slots; the ring holds one fewer (one slot stays
    /// empty to tell full from empty), so the default feed takes fifteen
    /// events before it starts refusing.
    static constexpr std::size_t kFeedCapacity = 16;

    FakeSessionHost(int width, int height);

    /// The table the desktop runs against; building it is not a const act
    /// because the table carries mutable access to this session.
    [[nodiscard]] visor::host::HostTable table() noexcept;

    /// Reads one received pixel; out-of-bounds reads as unlit.
    [[nodiscard]] bool get(visor::base::PointI p) const noexcept;

    /// The regions accepted since the last clear, in arrival order. The
    /// renderer submits strip windows, so a judgment about a widget's box
    /// asks which strips the box spans, not which rects the widgets pushed.
    [[nodiscard]] const std::vector<visor::base::RectI>& regions() const noexcept;

    /// Forgets the region log; counts stay. A judgment about one round calls
    /// this before the round's events go in.
    void clear_regions() noexcept;

    /// Successful region receptions, full frames included.
    [[nodiscard]] int flush_count() const noexcept;

    /// Refused submissions: wrong format, null pixels, an empty region, or a
    /// rectangle leaving the screen. The refusal line is the offscreen
    /// host's, so the desktop sees the same boundary in tests and on visor's
    /// real hosts.
    [[nodiscard]] int rejected_count() const noexcept;

    /// Feeds one synthetic event for the desktop to poll through the table.
    /// False when the feed is full: the newcomer is dropped and counted, the
    /// residents untouched. A test double reports its own drops.
    [[nodiscard]] bool push_event(const visor::host::Event& event) noexcept;

    /// Events the feed refused since construction.
    [[nodiscard]] int dropped_event_count() const noexcept;

    /// Advances the scripted clock; unsigned arithmetic wraps on purpose.
    void advance_ms(std::uint64_t delta) noexcept;

  private:
    static void flush_thunk(void* context, const visor::host::FlushRegion& region) noexcept;
    static bool poll_event_thunk(void* context, visor::host::Event& out) noexcept;
    static std::uint64_t clock_thunk(void* context) noexcept;
    void handle_flush(const visor::host::FlushRegion& region) noexcept;

    /// Copies one accepted region into the frame, one pixel at a time through
    /// the bit vocabulary. Strip-shaped regions arrive byte aligned, but the
    /// per-pixel path is the correctness floor for any bit offset and the
    /// sizes under test make the speed irrelevant.
    void blit(const visor::host::FlushRegion& region) noexcept;

    int width_ = 0;
    int height_ = 0;
    int flush_count_ = 0;
    int rejected_count_ = 0;
    int dropped_event_count_ = 0;
    std::uint64_t now_ms_ = 0;
    visor::base::container::RingQueue<visor::host::Event, kFeedCapacity> feed_{};
    std::vector<visor::base::RectI> regions_;
    std::vector<std::uint8_t> frame_; // same packing as the canvas, MSB first
};

} // namespace vesper::host
