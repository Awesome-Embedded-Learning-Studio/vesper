#include "cases.hpp"
#include "compositor/lane.hpp"
#include "testing.hpp"

#include <wayland-client-protocol.h>

#include <cstddef>
#include <cstdint>

namespace {

using vesper::testing::Lane;

/// One lane with globals surveyed and a pool standing: the shape every shm
/// judgment starts from.
class PoolLane {
  public:
    explicit PoolLane(std::size_t bytes) {
        if (!lane.up) {
            return;
        }
        ok = lane.client.survey_globals(lane.server) && lane.client.bind_shm() &&
             lane.client.make_pool(bytes) && lane.client.settle(lane.server);
    }

    bool ok = false;
    Lane lane;
};

// 64 words wide, 32 rows: a pool two buffers can share with padding left
// over.
constexpr std::size_t kPoolBytes = 64UZ * 32U * 4U;

void run_pool_and_buffer_tests() {
    PoolLane pool_lane(kPoolBytes);
    VESPER_CHECK(pool_lane.ok);
    Lane& lane = pool_lane.lane;

    // The pool's own words are the test's pen; a value written here is what
    // the desk's mapping must one day read back (the surface chapter's
    // judgment closes that loop).
    std::uint32_t* words = lane.client.pool_words();
    VESPER_CHECK(words != nullptr);
    words[0] = 0x1234'5678U;

    // One buffer mid-pool with a stride padded past its width: the shape
    // every real toolkit draws in.
    wl_buffer* padded = lane.client.make_buffer(64, 32, 8, 40, WL_SHM_FORMAT_XRGB8888);
    VESPER_CHECK(padded != nullptr);
    // And a second one sharing the same pool, the alpha flavor.
    wl_buffer* alpha = lane.client.make_buffer(64, 4, 4, 16, WL_SHM_FORMAT_ARGB8888);
    VESPER_CHECK(alpha != nullptr);
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK(lane.client.alive());

    // Buffers retire in the protocol's own order and the connection lives.
    lane.client.retire_buffer(padded);
    lane.client.retire_buffer(alpha);
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK(lane.client.alive());
}

void run_refusal_tests() {
    // A format the desk never offered kills the connection with the shm
    // error the spec names.
    {
        PoolLane pool_lane(kPoolBytes);
        VESPER_CHECK(pool_lane.ok);
        const wl_buffer* stranger = pool_lane.lane.client.make_buffer(0, 8, 8, 8, 0x7530U);
        VESPER_CHECK(stranger != nullptr);
        VESPER_CHECK(!pool_lane.lane.client.settle(pool_lane.lane.server));
        VESPER_CHECK(!pool_lane.lane.client.alive());
    }
    // A buffer whose last row leaves the mapping.
    {
        PoolLane pool_lane(kPoolBytes);
        VESPER_CHECK(pool_lane.ok);
        const wl_buffer* beyond =
            pool_lane.lane.client.make_buffer(2040, 32, 8, 40, WL_SHM_FORMAT_XRGB8888);
        VESPER_CHECK(beyond != nullptr);
        VESPER_CHECK(!pool_lane.lane.client.settle(pool_lane.lane.server));
        VESPER_CHECK(!pool_lane.lane.client.alive());
    }
    // A stride under one row of pixels is no stride at all.
    {
        PoolLane pool_lane(kPoolBytes);
        VESPER_CHECK(pool_lane.ok);
        const wl_buffer* thin =
            pool_lane.lane.client.make_buffer(0, 32, 8, 8, WL_SHM_FORMAT_XRGB8888);
        VESPER_CHECK(thin != nullptr);
        VESPER_CHECK(!pool_lane.lane.client.settle(pool_lane.lane.server));
        VESPER_CHECK(!pool_lane.lane.client.alive());
    }
}

void run_resize_tests() {
    PoolLane pool_lane(kPoolBytes);
    VESPER_CHECK(pool_lane.ok);
    Lane& lane = pool_lane.lane;

    // Grow the pool, then a buffer whose seat only exists in the grown
    // half: the desk's mremap is what makes the seat real.
    const std::size_t grown = kPoolBytes * 2U;
    VESPER_CHECK(lane.client.resize_pool(grown));
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK(lane.client.alive());

    const wl_buffer* tail =
        lane.client.make_buffer((kPoolBytes / 4U) + 64U, 32, 8, 40, WL_SHM_FORMAT_XRGB8888);
    VESPER_CHECK(tail != nullptr);
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK(lane.client.alive());

    // The grown mapping still ends: a buffer past it refuses all the same.
    const wl_buffer* beyond =
        lane.client.make_buffer((grown / 4U) + 64U, 32, 32, 40, WL_SHM_FORMAT_XRGB8888);
    VESPER_CHECK(beyond != nullptr);
    VESPER_CHECK(!lane.client.settle(lane.server));
    VESPER_CHECK(!lane.client.alive());
}

} // namespace

void run_compositor_shm_tests() {
    run_pool_and_buffer_tests();
    run_refusal_tests();
    run_resize_tests();
}
