#include "cases.hpp"
#include "compositor/lane.hpp"
#include "compositor/surface.hpp"
#include "testing.hpp"

#include <wayland-client-protocol.h>

#include <cstdint>

namespace {

using vesper::compositor::Surface;
using vesper::testing::Lane;

/// One lane standing on shm ground with a compositor bound: the shape every
/// surface judgment starts from.
class SurfaceLane {
  public:
    SurfaceLane() {
        if (!lane.up) {
            return;
        }
        ok = lane.client.survey_globals(lane.server) && lane.client.bind_shm() &&
             lane.client.bind_compositor() && lane.client.make_pool(64UZ * 32U * 4U) &&
             lane.client.settle(lane.server);
    }

    bool ok = false;
    Lane lane;
};

// The dusk-gold block: the desk's own amber, the color the map judgment
// looks for once surfaces land on the canvas.
constexpr std::uint32_t kAmber = 0xffc9'8a2dU;

void fill(std::uint32_t* words, int count, std::uint32_t value) noexcept {
    for (int i = 0; i < count; ++i) {
        words[i] = value;
    }
}

void run_commit_tests() {
    SurfaceLane surface_lane;
    VESPER_CHECK(surface_lane.ok);
    Lane& lane = surface_lane.lane;

    const wl_surface* surface = lane.client.make_surface();
    VESPER_CHECK(surface != nullptr);
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK_EQ(lane.server.surface_count(), 1);

    // A surface with nothing committed reads as empty, epoch zero.
    Surface::Frame frame = lane.server.surface_frame(0);
    VESPER_CHECK(frame.view.words == nullptr);
    VESPER_CHECK_EQ(frame.epoch, 0U);

    // The amber block goes in whole: fill, attach, damage, commit.
    wl_buffer* buffer = lane.client.make_buffer(0, 64, 32, 64, WL_SHM_FORMAT_XRGB8888);
    VESPER_CHECK(buffer != nullptr);
    fill(lane.client.pool_words(), 64 * 32, kAmber);
    lane.client.attach(buffer);
    lane.client.damage(0, 0, 64, 32);
    lane.client.commit();
    VESPER_CHECK(lane.client.settle(lane.server));

    // The mmap loop closes here: the guest's pen must read back through the
    // server's own mapping, word for word.
    frame = lane.server.surface_frame(0);
    VESPER_CHECK(frame.view.words != nullptr);
    VESPER_CHECK_EQ(frame.view.width, 64);
    VESPER_CHECK_EQ(frame.view.height, 32);
    VESPER_CHECK_EQ(frame.view.stride_words, 64UZ);
    VESPER_CHECK_EQ(frame.epoch, 1U);
    VESPER_CHECK_EQ(frame.view.words[0], kAmber);
    VESPER_CHECK_EQ(frame.view.words[(31UZ * 64UZ) + 31UZ], kAmber);

    // A second commit with the same buffer bumps the epoch again and the
    // words follow the guest's pen.
    lane.client.pool_words()[5] = 0xffff'ffffU;
    lane.client.attach(buffer);
    lane.client.damage(5, 0, 1, 1);
    lane.client.commit();
    VESPER_CHECK(lane.client.settle(lane.server));
    frame = lane.server.surface_frame(0);
    VESPER_CHECK_EQ(frame.epoch, 2U);
    VESPER_CHECK_EQ(frame.view.words[5], 0xffff'ffffU);
    // The damage the guest reported is the damage the desk reads, grown to
    // cover the union when more arrives.
    VESPER_CHECK_EQ(frame.damage.pos.x, 5);
    VESPER_CHECK_EQ(frame.damage.size.w, 1);
}

void run_release_tests() {
    SurfaceLane surface_lane;
    VESPER_CHECK(surface_lane.ok);
    Lane& lane = surface_lane.lane;

    const wl_surface* surface = lane.client.make_surface();
    VESPER_CHECK(surface != nullptr);
    wl_buffer* first = lane.client.make_buffer(0, 32, 32, 64, WL_SHM_FORMAT_XRGB8888);
    wl_buffer* second = lane.client.make_buffer(32UZ * 32UZ, 32, 32, 32, WL_SHM_FORMAT_XRGB8888);
    VESPER_CHECK(first != nullptr);
    VESPER_CHECK(second != nullptr);

    // The held buffer earns no release word; retiring one earns exactly one.
    lane.client.attach(first);
    lane.client.commit();
    VESPER_CHECK(lane.client.settle(lane.server));
    lane.server.flush_clients();
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK_EQ(lane.client.release_count(), 0);

    lane.client.attach(second);
    lane.client.commit();
    VESPER_CHECK(lane.client.settle(lane.server));
    lane.server.flush_clients();
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK_EQ(lane.client.release_count(), 1);

    // Re-committing the very buffer just retired holds it again: the desk's
    // word for it arrives when the next one replaces it.
    lane.client.attach(first);
    lane.client.commit();
    lane.client.attach(second);
    lane.client.commit();
    VESPER_CHECK(lane.client.settle(lane.server));
    lane.server.flush_clients();
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK_EQ(lane.client.release_count(), 3);
}

void run_frame_callback_tests() {
    SurfaceLane surface_lane;
    VESPER_CHECK(surface_lane.ok);
    Lane& lane = surface_lane.lane;
    const wl_surface* surface = lane.client.make_surface();
    VESPER_CHECK(surface != nullptr);

    // A parked callback waits for the desk's own presentation word: the
    // settle round carries words but fires no debts, end_frame fires them
    // all.
    lane.client.request_frame();
    lane.client.request_frame();
    lane.client.request_frame();
    lane.client.attach(nullptr);
    lane.client.commit();
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK_EQ(lane.client.frame_done_count(), 0);
    lane.server.set_frame_time_ms(42);
    lane.server.end_frame();
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK_EQ(lane.client.frame_done_count(), 3);

    // A null attach commit retires the content: the desk reads nothing
    // held, and the epoch holds still (nothing new arrived, only something
    // left).
    const Surface::Frame retired = lane.server.surface_frame(0);
    VESPER_CHECK(retired.view.words == nullptr);
    VESPER_CHECK_EQ(retired.epoch, 0U);
}

void run_destroy_tests() {
    SurfaceLane surface_lane;
    VESPER_CHECK(surface_lane.ok);
    Lane& lane = surface_lane.lane;

    const wl_surface* surface = lane.client.make_surface();
    VESPER_CHECK(surface != nullptr);
    wl_buffer* held = lane.client.make_buffer(0, 32, 32, 32, WL_SHM_FORMAT_XRGB8888);
    VESPER_CHECK(held != nullptr);
    lane.client.attach(held);
    lane.client.commit();
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK_EQ(lane.server.surface_count(), 1);

    // The surface's own goodbye releases what it held and strikes the book.
    static_cast<void>(surface);
    lane.client.retire_surface();
    VESPER_CHECK(lane.client.settle(lane.server));
    lane.server.flush_clients();
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK_EQ(lane.server.surface_count(), 0);
    VESPER_CHECK_EQ(lane.client.release_count(), 1);
}

void run_destroyed_buffer_tests() {
    SurfaceLane surface_lane;
    VESPER_CHECK(surface_lane.ok);
    Lane& lane = surface_lane.lane;
    VESPER_CHECK(lane.client.make_surface() != nullptr);
    wl_buffer* buffer = lane.client.make_buffer(0, 32, 32, 32, WL_SHM_FORMAT_XRGB8888);
    fill(lane.client.pool_words(), 32 * 32, kAmber);
    lane.client.attach(buffer);
    // Destroying the protocol object before commit must keep its pixels alive.
    lane.client.retire_buffer(buffer);
    lane.client.commit();
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK_EQ(lane.server.surface_frame(0).view.words[0], kAmber);
    lane.server.end_frame();
    // A commit without attach retains the current buffer.
    lane.client.damage(0, 0, 1, 1);
    lane.client.commit();
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK_EQ(lane.server.surface_frame(0).view.words[0], kAmber);
    lane.client.retire_surface();
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK_EQ(lane.server.surface_count(), 0);
}

void run_disconnect_callback_tests() {
    SurfaceLane surface_lane;
    VESPER_CHECK(surface_lane.ok);
    Lane& lane = surface_lane.lane;
    wl_surface* surface = lane.client.make_surface();
    VESPER_CHECK(surface != nullptr);
    wl_callback* callback = wl_surface_frame(surface);
    lane.client.commit();
    VESPER_CHECK(lane.client.settle(lane.server));
    // Destroying the local proxy leaves the server-side callback pending.
    wl_callback_destroy(callback);
    lane.client.disconnect();
    VESPER_CHECK(lane.server.dispatch(0));
    lane.server.end_frame();
    VESPER_CHECK_EQ(lane.server.surface_count(), 0);
}

} // namespace

void run_compositor_surface_tests() {
    run_commit_tests();
    run_release_tests();
    run_frame_callback_tests();
    run_destroy_tests();
    run_destroyed_buffer_tests();
    run_disconnect_callback_tests();
}
