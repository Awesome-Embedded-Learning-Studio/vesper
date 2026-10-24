#include "cases.hpp"
#include "compositor/server.hpp"
#include "compositor/shell.hpp"
#include "compositor/sparring_client.hpp"
#include "testing.hpp"
#include "ui/shell_desk/shell_desk.hpp"

#include "fake_color_host.hpp"
#include "fonts/demo_font.hpp"
#include "ui/desktop/desktop.hpp"
#include "ui/style/theme.hpp"
#include "ui/window_frame/window_frame.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/canvas/pixel_traits.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/run_loop.hpp"

#include <sys/socket.h>

#include <wayland-client-protocol.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

using vesper::compositor::Server;
using vesper::compositor::ShellSurface;
using vesper::desktop::Desktop;
using vesper::desktop::ShellDesk;
using vesper::host::FakeColorHost;
using vesper::testing::SparringClient;
using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;

// A thousand frames on one buffer: the roadmap's own no-deadlock number.
constexpr int kFrameCount = 1000;

constexpr SizeI kScreen{.w = 320, .h = 256};
constexpr RectI kScreenBounds{.pos = PointI{0, 0}, .size = kScreen};
using Frame = visor::canvas::Canvas<kScreen, visor::canvas::Xrgb8888Traits>;
// The render strip: 64 rows tall, the same strip shape the fbdev lane
// draws with -- the damage judgment counts these.
using Strip = visor::canvas::Canvas<SizeI{.w = 320, .h = 64}, visor::canvas::Xrgb8888Traits>;

/// The desk's cadence witness: one guest on one seat, presenting on the
/// desk's own tick.
struct CadenceScene {
    FakeColorHost host{320, 256};
    visor::ui::InputRouter input{};
    visor::ui::DirtyRegions dirty{};
    Frame frame{};
    Strip strip{};
    Desktop desktop{kScreenBounds, "VESPER", vesper::fonts::kDemoFont,
                    vesper::desktop::style::kColorDeskTheme};
    Server server;
    ShellDesk desk{desktop, server, vesper::fonts::kDemoFont};
    SparringClient client;

    [[nodiscard]] bool build() {
        std::array<int, 2> fds{};
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()) != 0) {
            return false;
        }
        desktop.set_dirty_sink(&dirty);
        dirty.push(kScreenBounds);
        if (server.adopt_client(fds[1]) == nullptr || !client.connect_fd(fds[0])) {
            return false;
        }
        const bool armed = client.survey_globals(server) && client.bind_shm() &&
                           client.bind_compositor() && client.bind_wm_base() &&
                           client.make_pool(64UZ * 64U * 4U) && client.make_surface() != nullptr &&
                           client.make_toplevel(server, "Ticker");
        if (!armed) {
            return false;
        }
        wl_buffer* buffer = client.make_buffer(0, 64, 64, 64, WL_SHM_FORMAT_XRGB8888);
        if (buffer == nullptr) {
            return false;
        }
        client.attach(buffer);
        client.damage(0, 0, 64, 64);
        client.commit();
        if (!client.settle(server)) {
            return false;
        }
        round();
        return true;
    }

    /// One desk round: serve, let the guests' words land, collect the
    /// debts, render, present.
    void round() {
        desk.pump();
        static_cast<void>(client.settle(server));
        desk.collect();
        render();
        server.note_time_ms(tick++);
        desk.present();
        static_cast<void>(client.settle(server));
    }

    void render() {
        static_cast<void>(
            visor::ui::run_dirty(host.table(), desktop, input, strip, kScreen, dirty));
    }

    std::uint32_t tick = 1;
};

void run_frame_rate_tests() {
    // The roadmap's C4 judgment: a single-buffer guest submits a thousand
    // frames on the desk's cadence -- write after release, commit, wait for
    // the frame's done word -- and never once deadlocks, its frame rate
    // exactly the desk's own tick count.
    CadenceScene scene;
    VESPER_CHECK(scene.build());
    wl_buffer* buffer = scene.client.make_buffer(0, 64, 64, 64, WL_SHM_FORMAT_XRGB8888);
    VESPER_CHECK(buffer != nullptr);

    bool cadence_held = true;
    for (int i = 0; i < kFrameCount; ++i) {
        // The single buffer is free because the desk's last presentation
        // freed it -- a guest writing before that word would tear, and a
        // desk that never sends it would hang this loop forever. The
        // counts below are the wait, made visible instead of fatal.
        for (int word = 0; word < 64 * 64; ++word) {
            scene.client.pool_words()[static_cast<std::size_t>(word)] =
                (0xff00'0000U | (static_cast<unsigned>(i) & 0xffU));
        }
        scene.client.request_frame();
        scene.client.attach(buffer);
        scene.client.damage(0, 0, 64, 64);
        scene.client.commit();
        if (!scene.client.settle(scene.server)) {
            cadence_held = false;
            break;
        }
        // The desk's render consumed the pixels; the presentation beat
        // sends the callback and frees the buffer.
        scene.desk.collect();
        scene.render();
        scene.server.note_time_ms(scene.tick++);
        scene.desk.present();
        if (!scene.client.settle(scene.server)) {
            cadence_held = false;
            break;
        }
        if (scene.client.frame_done_count() != i + 1 || scene.client.release_count() != i + 2) {
            cadence_held = false;
            break;
        }
    }
    VESPER_CHECK(cadence_held);
    VESPER_CHECK_EQ(scene.client.frame_done_count(), kFrameCount);
    VESPER_CHECK_EQ(scene.client.release_count(), kFrameCount + 1);
    VESPER_CHECK(scene.client.alive());

    // The last frame's mark is on the desk, word for word.
    const ShellSurface* shell = scene.server.shell_surface(0);
    const vesper::desktop::WindowFrame* window = scene.desk.frame_of(shell);
    VESPER_CHECK(window != nullptr);
    if (window != nullptr) {
        const RectI content = window->content_area();
        VESPER_CHECK_EQ(scene.host.get(PointI{content.pos.x + 8, content.pos.y + 8}),
                        (0xff00'0000U | (static_cast<unsigned>(kFrameCount - 1) & 0xffU)));
    }
}

void run_damage_tests() {
    // Damage only reports change: a frame that dirtied one corner costs
    // the strips that corner touches, never the whole screen.
    CadenceScene scene;
    VESPER_CHECK(scene.build());

    // The first frame is already down (whole-screen debt at build); start
    // the ledger fresh.
    scene.host.clear_regions();

    scene.client.pool_words()[(40UZ * 64UZ) + 40UZ] = 0xffff'ffffU;
    // A commit with a corner-sized damage word.
    wl_buffer* buffer = scene.client.make_buffer(0, 64, 64, 64, WL_SHM_FORMAT_XRGB8888);
    VESPER_CHECK(buffer != nullptr);
    scene.client.attach(buffer);
    scene.client.damage(40, 40, 8, 8);
    scene.client.commit();
    scene.round();

    const auto& regions = scene.host.regions();
    VESPER_CHECK(!regions.empty());
    // The strips of a 256-high screen over 64-high strips number four; a
    // corner-sized damage word touches exactly one of them. Full-screen
    // debt would submit all four.
    const ShellSurface* shell = scene.server.shell_surface(0);
    const vesper::desktop::WindowFrame* window = scene.desk.frame_of(shell);
    VESPER_CHECK(window != nullptr);
    if (window == nullptr) {
        return;
    }
    const int corner_y = window->content_area().pos.y + 40;
    std::size_t corner_strips = 0;
    for (const RectI& region : regions) {
        if (region.pos.y <= corner_y && region.pos.y + region.size.h > corner_y) {
            ++corner_strips;
        }
    }
    VESPER_CHECK_EQ(corner_strips, 1UZ);
    VESPER_CHECK_EQ(regions.size(), 1UZ);
}

} // namespace

void run_compositor_cadence_tests() {
    run_frame_rate_tests();
    run_damage_tests();
}
