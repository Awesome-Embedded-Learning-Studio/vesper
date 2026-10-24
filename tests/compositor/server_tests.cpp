#include "cases.hpp"
#include "compositor/lane.hpp"
#include "compositor/server.hpp"
#include "compositor/sparring_client.hpp"
#include "testing.hpp"

#include <sys/socket.h>
#include <wayland-client-protocol.h>

#include <array>
#include <cstdint>

namespace {

using vesper::compositor::Server;
using vesper::testing::Lane;
using vesper::testing::SparringClient;

struct OutputState {
    int width = 0;
    int height = 0;
    bool done = false;
};

void output_geometry(void* /*data*/, wl_output* /*output*/, std::int32_t /*x*/, std::int32_t /*y*/,
                     std::int32_t /*physical_width*/, std::int32_t /*physical_height*/,
                     std::int32_t /*subpixel*/, const char* /*make*/, const char* /*model*/,
                     std::int32_t /*transform*/) {}

void output_mode(void* data, wl_output* /*output*/, std::uint32_t flags, std::int32_t width,
                 std::int32_t height, std::int32_t /*refresh*/) {
    if ((flags & WL_OUTPUT_MODE_CURRENT) != 0) {
        auto& state = *static_cast<OutputState*>(data);
        state.width = width;
        state.height = height;
    }
}

void output_done(void* data, wl_output* /*output*/) {
    static_cast<OutputState*>(data)->done = true;
}

void output_scale(void* /*data*/, wl_output* /*output*/, std::int32_t /*scale*/) {}

const wl_output_listener kOutputListener{
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
    .name = nullptr,
    .description = nullptr,
};

void run_output_tests() {
    Lane lane;
    lane.server.note_output_extent(320, 256);
    VESPER_CHECK(lane.client.survey_globals(lane.server));
    const auto* global = lane.client.find_global("wl_output");
    VESPER_CHECK(global != nullptr);
    if (global == nullptr) {
        return;
    }
    wl_registry* registry = wl_display_get_registry(lane.client.display());
    auto* output =
        static_cast<wl_output*>(wl_registry_bind(registry, global->name, &wl_output_interface, 2));
    OutputState state;
    wl_output_add_listener(output, &kOutputListener, &state);
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK_EQ(state.width, 320);
    VESPER_CHECK_EQ(state.height, 256);
    VESPER_CHECK(state.done);
    wl_output_destroy(output);
    wl_registry_destroy(registry);
}

void run_lifecycle_tests() {
    // A server comes up with its loop pollable and nobody connected.
    Server server;
    VESPER_CHECK(server.valid());
    VESPER_CHECK(server.event_fd() >= 0);
    VESPER_CHECK_EQ(server.client_count(), 0);
    // A quiet dispatch round is a success, not an error.
    VESPER_CHECK(server.dispatch(0));
}

void run_globals_tests() {
    Lane lane;
    VESPER_CHECK(lane.up);
    VESPER_CHECK(lane.client.survey_globals(lane.server));
    VESPER_CHECK_EQ(lane.client.version_of("wl_compositor"), 4U);
    VESPER_CHECK_EQ(lane.client.version_of("wl_shm"), 1U);
    VESPER_CHECK_EQ(lane.client.version_of("wl_seat"), 5U);
    VESPER_CHECK_EQ(lane.client.version_of("xdg_wm_base"), 1U);
    // Xwayland sees the desk output; clipboard support remains absent.
    VESPER_CHECK_EQ(lane.client.version_of("wl_output"), 2U);
    VESPER_CHECK_EQ(lane.client.version_of("wl_data_device_manager"), 0U);
    VESPER_CHECK_EQ(lane.server.client_count(), 1);
}

void run_two_clients_tests() {
    Lane lane;
    VESPER_CHECK(lane.up);
    SparringClient second;
    std::array<int, 2> fds{};
    VESPER_CHECK_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()), 0);
    VESPER_CHECK(lane.server.adopt_client(fds[1]) != nullptr);
    VESPER_CHECK(second.connect_fd(fds[0]));

    // Both guests see the same globals.
    VESPER_CHECK(lane.client.survey_globals(lane.server));
    VESPER_CHECK(second.survey_globals(lane.server));
    VESPER_CHECK_EQ(lane.server.client_count(), 2);

    // One guest leaving is one guest fewer, the other one untouched.
    second.disconnect();
    VESPER_CHECK(lane.server.dispatch(0));
    VESPER_CHECK_EQ(lane.server.client_count(), 1);
    VESPER_CHECK(lane.client.survey_globals(lane.server));
}

} // namespace

void run_compositor_server_tests() {
    run_lifecycle_tests();
    run_globals_tests();
    run_output_tests();
    run_two_clients_tests();
}
