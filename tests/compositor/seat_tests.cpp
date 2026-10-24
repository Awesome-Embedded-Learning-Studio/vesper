#include "cases.hpp"
#include "compositor/lane.hpp"
#include "compositor/seat.hpp"
#include "compositor/shell.hpp"
#include "compositor/sparring_client.hpp"
#include "testing.hpp"
#include "ui/wayland_canvas/wayland_canvas.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/bindings/events/pointer_words.hpp"

#include <sys/socket.h>

#include <wayland-client-protocol.h>

#include <array>

#include <cstdint>

namespace {

using vesper::compositor::Seat;
using vesper::compositor::ShellSurface;
using vesper::desktop::WaylandCanvas;
using vesper::testing::Lane;
using vesper::testing::SparringClient;
using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;

// BTN_LEFT's own Linux number, and two evdev keys the judgments feed.
constexpr std::uint32_t kBtnLeft = 0x110;
constexpr std::uint32_t kKeyA = 30;     // evdev KEY_A
constexpr std::uint32_t kKeyLeft = 105; // evdev KEY_LEFT, visor's 0x112 folded back

/// One lane with a mapped toplevel and both seat hands bound: the shape
/// every input judgment starts from.
class SeatLane {
  public:
    SeatLane() {
        if (!lane.up) {
            return;
        }
        const bool armed =
            lane.client.survey_globals(lane.server) && lane.client.bind_shm() &&
            lane.client.bind_compositor() && lane.client.bind_wm_base() &&
            lane.client.bind_seat(lane.server) && lane.client.make_pool(64UZ * 32U * 4U) &&
            lane.client.settle(lane.server) && lane.client.make_surface() != nullptr &&
            lane.client.make_toplevel(lane.server, "Typist");
        if (!armed) {
            return;
        }
        wl_buffer* buffer = lane.client.make_buffer(0, 64, 32, 64, WL_SHM_FORMAT_XRGB8888);
        if (buffer == nullptr) {
            return;
        }
        lane.client.attach(buffer);
        lane.client.commit();
        ok = lane.client.settle(lane.server);
    }

    bool ok = false;
    Lane lane;
};

void run_keyboard_tests() {
    SeatLane seat_lane;
    VESPER_CHECK(seat_lane.ok);
    Lane& lane = seat_lane.lane;
    Seat& seat = lane.server.seat();
    ShellSurface* shell = lane.server.shell_surface(0);
    VESPER_CHECK(shell != nullptr);
    if (shell == nullptr) {
        return;
    }

    // The keymap file rode with the keyboard object.
    VESPER_CHECK(lane.client.keymap_bytes() > 0);

    // Focus crosses once; keys ride the focus, code for code.
    seat.keyboard_enter(shell->surface());
    lane.server.note_time_ms(1000);
    seat.key(kKeyA, true);
    seat.key(kKeyA, false);
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK_EQ(lane.client.keyboard_enters(), 1);
    VESPER_CHECK_EQ(lane.client.key_count(), 2UZ);
    const auto* first = lane.client.key_at(0);
    const auto* second = lane.client.key_at(1);
    if (first != nullptr) {
        VESPER_CHECK_EQ(first->code, kKeyA);
        VESPER_CHECK(first->pressed);
    }
    if (second != nullptr) {
        VESPER_CHECK_EQ(second->code, kKeyA);
        VESPER_CHECK(!second->pressed);
    }

    // The desk's named arrow arrives as evdev's own number.
    seat.key(0x112, true);
    VESPER_CHECK(lane.client.settle(lane.server));
    const auto* arrow = lane.client.key_at(2);
    VESPER_CHECK(arrow != nullptr);
    if (arrow != nullptr) {
        VESPER_CHECK_EQ(arrow->code, kKeyLeft);
    }

    // The modifier set rides as the xkb mask.
    seat.set_modifiers(visor::host::Modifiers{.shift = true});
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK_EQ(lane.client.last_modifiers(), 1U);

    // Leaving is one word, and further keys land nowhere.
    seat.keyboard_leave();
    seat.key(kKeyA, true);
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK_EQ(lane.client.keyboard_leaves(), 1);
    VESPER_CHECK_EQ(lane.client.key_count(), 3UZ);
}

void run_pointer_tests() {
    SeatLane seat_lane;
    VESPER_CHECK(seat_lane.ok);
    Lane& lane = seat_lane.lane;
    Seat& seat = lane.server.seat();
    ShellSurface* shell = lane.server.shell_surface(0);
    VESPER_CHECK(shell != nullptr);
    if (shell == nullptr) {
        return;
    }

    // Enter carries its position with it; motion and button follow.
    seat.pointer_enter(shell->surface(), PointI{2, 3});
    lane.server.note_time_ms(2000);
    seat.pointer_motion(PointI{5, 6});
    seat.pointer_button(kBtnLeft, true);
    seat.pointer_button(kBtnLeft, false);
    seat.pointer_wheel(2, -1);
    VESPER_CHECK(lane.client.settle(lane.server));

    VESPER_CHECK_EQ(lane.client.pointer_enters(), 1);
    VESPER_CHECK_EQ(lane.client.motion_count(), 2UZ);
    const auto* enter_at = lane.client.motion_at(0);
    const auto* moved = lane.client.motion_at(1);
    VESPER_CHECK(enter_at != nullptr);
    VESPER_CHECK(moved != nullptr);
    if (enter_at != nullptr && moved != nullptr) {
        VESPER_CHECK_EQ(enter_at->x, 2);
        VESPER_CHECK_EQ(enter_at->y, 3);
        VESPER_CHECK_EQ(moved->x, 5);
        VESPER_CHECK_EQ(moved->y, 6);
    }

    VESPER_CHECK_EQ(lane.client.button_count(), 2UZ);
    const auto* pressed = lane.client.button_at(0);
    const auto* released = lane.client.button_at(1);
    VESPER_CHECK(pressed != nullptr);
    VESPER_CHECK(released != nullptr);
    if (pressed != nullptr && released != nullptr) {
        VESPER_CHECK_EQ(pressed->code, kBtnLeft);
        VESPER_CHECK(pressed->pressed);
        VESPER_CHECK_EQ(released->code, kBtnLeft);
        VESPER_CHECK(!released->pressed);
    }

    // Two notches right, one notch up: 240 and -120 on their own axes.
    VESPER_CHECK_EQ(lane.client.wheel_count(), 2UZ);
    const auto* horizontal = lane.client.wheel_at(0);
    const auto* vertical = lane.client.wheel_at(1);
    VESPER_CHECK(horizontal != nullptr);
    VESPER_CHECK(vertical != nullptr);
    if (horizontal != nullptr && vertical != nullptr) {
        VESPER_CHECK_EQ(horizontal->axis, 1U); // WL_POINTER_AXIS_HORIZONTAL_SCROLL
        VESPER_CHECK_EQ(horizontal->value, 240);
        VESPER_CHECK_EQ(vertical->axis, 0U); // WL_POINTER_AXIS_VERTICAL_SCROLL
        VESPER_CHECK_EQ(vertical->value, -120);
    }

    // Re-entering the same face carries no second enter word.
    seat.pointer_enter(shell->surface(), PointI{7, 7});
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK_EQ(lane.client.pointer_enters(), 1);

    seat.pointer_leave();
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK_EQ(lane.client.pointer_leaves(), 1);
}

void run_focus_isolation_tests() {
    SeatLane seat_lane;
    VESPER_CHECK(seat_lane.ok);
    Lane& lane = seat_lane.lane;
    Seat& seat = lane.server.seat();
    ShellSurface* first_shell = lane.server.shell_surface(0);
    VESPER_CHECK(first_shell != nullptr);
    if (first_shell == nullptr) {
        return;
    }
    seat.keyboard_enter(first_shell->surface());
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK_EQ(lane.client.keyboard_enters(), 1);

    // A second guest maps its own face; focus crosses to it.
    SparringClient second;
    std::array<int, 2> fds{};
    VESPER_CHECK_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()), 0);
    VESPER_CHECK(lane.server.adopt_client(fds[1]) != nullptr);
    VESPER_CHECK(second.connect_fd(fds[0]));
    VESPER_CHECK(second.survey_globals(lane.server) && second.bind_shm() &&
                 second.bind_compositor() && second.bind_wm_base() &&
                 second.bind_seat(lane.server) && second.make_pool(32UZ * 32U * 4U) &&
                 second.make_surface() != nullptr && second.make_toplevel(lane.server, "Rival") &&
                 second.settle(lane.server));
    VESPER_CHECK(lane.server.shell_surface_count() == 2);
    ShellSurface* second_shell = lane.server.shell_surface(1);
    VESPER_CHECK(second_shell != nullptr);
    wl_buffer* buffer = second.make_buffer(0, 32, 32, 32, WL_SHM_FORMAT_XRGB8888);
    VESPER_CHECK(buffer != nullptr);
    second.attach(buffer);
    second.commit();
    VESPER_CHECK(second.settle(lane.server));

    seat.keyboard_enter(second_shell->surface());
    seat.key(kKeyA, true);
    VESPER_CHECK(lane.client.settle(lane.server));
    VESPER_CHECK(second.settle(lane.server));

    // The first guest heard the leave and not the key; the second heard
    // the enter and the key.
    VESPER_CHECK_EQ(lane.client.keyboard_leaves(), 1);
    VESPER_CHECK_EQ(lane.client.key_count(), 0UZ);
    VESPER_CHECK_EQ(second.keyboard_enters(), 1);
    VESPER_CHECK_EQ(second.key_count(), 1UZ);
    const auto* crossed = second.key_at(0);
    VESPER_CHECK(crossed != nullptr);
    if (crossed != nullptr) {
        VESPER_CHECK_EQ(crossed->code, kKeyA);
    }
}

void run_canvas_forwarding_tests() {
    // The widget half of C2: the canvas folds positions into the surface's
    // own coordinates and rides every word down the seat's wire.
    SeatLane seat_lane;
    VESPER_CHECK(seat_lane.ok);
    Lane& lane = seat_lane.lane;
    ShellSurface* shell = lane.server.shell_surface(0);
    VESPER_CHECK(shell != nullptr);
    if (shell == nullptr) {
        return;
    }
    WaylandCanvas canvas{RectI{.pos = PointI{100, 60}, .size = SizeI{.w = 64, .h = 32}}, shell,
                         &lane.server.seat()};

    lane.server.seat().keyboard_enter(shell->surface());
    canvas.on_key_down(kKeyA, visor::host::Modifiers{});
    canvas.on_key_up(kKeyA, visor::host::Modifiers{});
    canvas.on_pointer_down(PointI{103, 65}, visor::host::Modifiers{});
    canvas.on_pointer_move(PointI{110, 70});
    canvas.on_pointer_up(PointI{120, 70});
    canvas.on_pointer_wheel(visor::host::PointerWheel{.dx = 0, .dy = -1, .pos = PointI{0, 0}});
    VESPER_CHECK(lane.client.settle(lane.server));

    VESPER_CHECK_EQ(lane.client.key_count(), 2UZ);
    VESPER_CHECK_EQ(lane.client.button_count(), 2UZ);
    VESPER_CHECK_EQ(lane.client.wheel_count(), 1UZ);
    // The press at desk point (103, 65) crossed as surface point (3, 5).
    const auto* enter_at = lane.client.motion_at(0);
    VESPER_CHECK(enter_at != nullptr);
    if (enter_at != nullptr) {
        VESPER_CHECK_EQ(enter_at->x, 3);
        VESPER_CHECK_EQ(enter_at->y, 5);
    }
}

} // namespace

void run_compositor_seat_tests() {
    run_keyboard_tests();
    run_pointer_tests();
    run_focus_isolation_tests();
    run_canvas_forwarding_tests();
}
