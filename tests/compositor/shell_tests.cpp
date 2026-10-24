#include "cases.hpp"
#include "compositor/lane.hpp"
#include "compositor/shell.hpp"
#include "testing.hpp"

#include "fake_color_host.hpp"
#include "fonts/demo_font.hpp"
#include "ui/desktop/desktop.hpp"
#include "ui/style/theme.hpp"
#include "ui/wayland_canvas/wayland_canvas.hpp"
#include "ui/window_frame/window_frame.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/canvas/pixel_traits.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/run_loop.hpp"

#include <wayland-client-protocol.h>

#include <cstdint>
#include <cstring>

namespace {

using vesper::compositor::ShellSurface;
using vesper::desktop::Desktop;
using vesper::desktop::WaylandCanvas;
using vesper::desktop::WindowFrame;
using vesper::host::FakeColorHost;
using vesper::testing::Lane;
using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;

// The dusk-gold block: the desk's own amber, the word the map judgment
// looks for on the canvas.
constexpr std::uint32_t kAmber = 0xffc9'8a2dU;

/// The map/unmap tally the hooks count into.
struct HookTally {
    int maps = 0;
    int unmaps = 0;
};

void on_shell_map(ShellSurface& /*shell*/, void* context) noexcept {
    ++static_cast<HookTally*>(context)->maps;
}

void on_shell_unmap(ShellSurface& shell, void* context) noexcept {
    VESPER_CHECK(!shell.mapped());
    ++static_cast<HookTally*>(context)->unmaps;
}

/// One lane standing on configured shell ground: shm, surface and a
/// toplevel born and acked, ready to map a face.
class ShellLane {
  public:
    explicit ShellLane(const char* title = "Amber", bool auto_ack = true) {
        if (!lane.up) {
            return;
        }
        lane.server.set_shell_hooks(vesper::compositor::ShellHooks{
            .on_map = &on_shell_map,
            .on_unmap = &on_shell_unmap,
            .context = &tally,
        });
        ok = lane.client.survey_globals(lane.server) && lane.client.bind_shm() &&
             lane.client.bind_compositor() && lane.client.bind_wm_base() &&
             lane.client.make_pool(64UZ * 32U * 4U) && lane.client.settle(lane.server) &&
             lane.client.make_surface() != nullptr &&
             lane.client.make_toplevel(lane.server, title, auto_ack);
    }

    bool ok = false;
    HookTally tally;
    Lane lane;
};

void run_map_tests() {
    ShellLane shell_lane;
    VESPER_CHECK(shell_lane.ok);
    Lane& lane = shell_lane.lane;

    VESPER_CHECK_EQ(lane.server.shell_surface_count(), 1);
    const ShellSurface* shell = lane.server.shell_surface(0);
    VESPER_CHECK(shell != nullptr);
    if (shell == nullptr) {
        return; // the checks below read through this pointer
    }
    VESPER_CHECK(!shell->mapped());
    VESPER_CHECK(std::strcmp(shell->title(), "Amber") == 0);

    wl_buffer* buffer = lane.client.make_buffer(0, 64, 32, 64, WL_SHM_FORMAT_XRGB8888);
    VESPER_CHECK(buffer != nullptr);
    for (int i = 0; i < 64 * 32; ++i) {
        lane.client.pool_words()[i] = kAmber;
    }
    lane.client.attach(buffer);
    lane.client.damage(0, 0, 64, 32);
    lane.client.commit();
    VESPER_CHECK(lane.client.settle(lane.server));

    VESPER_CHECK(shell->mapped());
    VESPER_CHECK_EQ(shell->width(), 64);
    VESPER_CHECK_EQ(shell->height(), 32);
    VESPER_CHECK_EQ(shell_lane.tally.maps, 1);
    VESPER_CHECK_EQ(shell_lane.tally.unmaps, 0);

    // A null commit takes the face down; a buffered commit brings it back.
    lane.client.attach(nullptr);
    lane.client.commit();
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK(!shell->mapped());
    VESPER_CHECK_EQ(shell_lane.tally.unmaps, 1);
    lane.client.attach(buffer);
    lane.client.commit();
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK(shell->mapped());
    VESPER_CHECK_EQ(shell_lane.tally.maps, 2);
}

void run_close_tests() {
    ShellLane shell_lane;
    VESPER_CHECK(shell_lane.ok);
    Lane& lane = shell_lane.lane;

    wl_buffer* buffer = lane.client.make_buffer(0, 64, 32, 64, WL_SHM_FORMAT_XRGB8888);
    VESPER_CHECK(buffer != nullptr);
    lane.client.attach(buffer);
    lane.client.commit();
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK(lane.server.shell_surface(0)->mapped());

    // The desk's close word crosses the wire and lands as the toplevel's
    // close event, the guest's own choice how to answer.
    lane.server.shell_surface(0)->request_close();
    lane.server.flush_clients();
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK(lane.client.close_requested());
}

void run_unconfigured_refusal_tests() {
    ShellLane shell_lane("Impatient", false);
    VESPER_CHECK(shell_lane.ok);
    Lane& lane = shell_lane.lane;

    // The guest drew before acking any configure: the one xdg mistake with
    // its own word. The connection ends and the hooks stay quiet (a face
    // never mapped is a face never unmapped).
    wl_buffer* buffer = lane.client.make_buffer(0, 32, 32, 32, WL_SHM_FORMAT_XRGB8888);
    VESPER_CHECK(buffer != nullptr);
    lane.client.attach(buffer);
    lane.client.commit();
    VESPER_CHECK(!lane.client.settle(lane.server));
    VESPER_CHECK(!lane.client.alive());
    VESPER_CHECK_EQ(shell_lane.tally.maps, 0);
}

void run_destroy_tests() {
    ShellLane shell_lane;
    VESPER_CHECK(shell_lane.ok);
    Lane& lane = shell_lane.lane;

    wl_buffer* buffer = lane.client.make_buffer(0, 64, 32, 64, WL_SHM_FORMAT_XRGB8888);
    VESPER_CHECK(buffer != nullptr);
    lane.client.attach(buffer);
    lane.client.commit();
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK_EQ(lane.server.shell_surface_count(), 1);

    // The guest's goodbye strikes the shell book and the desk hears the
    // unmap; the bare surface itself stays on the desk's surface book.
    lane.client.retire_toplevel();
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK_EQ(lane.server.shell_surface_count(), 0);
    VESPER_CHECK_EQ(lane.server.surface_count(), 1);
    VESPER_CHECK_EQ(shell_lane.tally.unmaps, 1);
}

void run_desk_pixel_tests() {
    // The C1 judgment itself: a guest's amber block, laid on the desk's
    // canvas through the whole chain -- shm buffer, xdg toplevel, window
    // frame, canvas widget, renderer -- reads back word for word.
    ShellLane shell_lane("Block");
    VESPER_CHECK(shell_lane.ok);
    Lane& lane = shell_lane.lane;

    wl_buffer* buffer = lane.client.make_buffer(0, 64, 32, 64, WL_SHM_FORMAT_XRGB8888);
    VESPER_CHECK(buffer != nullptr);
    for (int i = 0; i < 64 * 32; ++i) {
        lane.client.pool_words()[i] = kAmber;
    }
    lane.client.attach(buffer);
    lane.client.damage(0, 0, 64, 32);
    lane.client.commit();
    VESPER_CHECK(lane.client.settle(lane.server));
    ShellSurface* shell = lane.server.shell_surface(0);
    VESPER_CHECK(shell != nullptr);
    if (shell == nullptr) {
        return;
    }
    VESPER_CHECK(shell->mapped());

    constexpr SizeI screen{.w = 320, .h = 240};
    constexpr RectI screen_bounds{.pos = PointI{0, 0}, .size = screen};
    using Frame = visor::canvas::Canvas<screen, visor::canvas::Xrgb8888Traits>;

    FakeColorHost host{320, 240};
    visor::ui::InputRouter input{};
    visor::ui::DirtyRegions dirty{};
    Frame frame{};
    Desktop desktop{screen_bounds, "VESPER", vesper::fonts::kDemoFont,
                    vesper::desktop::style::kColorDeskTheme};
    WindowFrame win{RectI{.pos = PointI{40, 30}, .size = SizeI{.w = 120, .h = 80}}, "BLOCK",
                    vesper::fonts::kDemoFont, screen_bounds};
    WaylandCanvas canvas{win.content_area(), shell};
    VESPER_CHECK(win.initialize());
    VESPER_CHECK(win.set_content(&canvas));
    VESPER_CHECK(desktop.add_child(&win));
    desktop.set_dirty_sink(&dirty);

    // The guest's commit became the widget's debt; one round clears it.
    canvas.note_commit();
    static_cast<void>(visor::ui::run_dirty(host.table(), desktop, input, frame, screen, dirty));

    const RectI content = win.content_area();
    VESPER_CHECK_EQ(host.get(PointI{content.pos.x + 2, content.pos.y + 2}), kAmber);
    VESPER_CHECK_EQ(host.get(PointI{content.pos.x + 63, content.pos.y + 31}), kAmber);
    // The buffer is 64 wide inside a wider content area: past its right
    // edge the desk's own ground shows, not the guest's block.
    VESPER_CHECK(host.get(PointI{content.pos.x + 70, content.pos.y + 2}) != kAmber);
}

} // namespace

void run_compositor_shell_tests() {
    run_map_tests();
    run_close_tests();
    run_unconfigured_refusal_tests();
    run_destroy_tests();
    run_desk_pixel_tests();
}
