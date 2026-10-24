#include "cases.hpp"
#include "testing.hpp"

#include <cstring>
#include <unistd.h>

// C++ fixes the entry point's argument type as char**.
int main(int /*argc*/, char** argv) { // NOLINT(misc-const-correctness)
    // launch() execs this binary as a deliberately silent Xwayland child.
    // The timeout test proves that readiness failure cannot hang the desk.
    if (std::strcmp(argv[0], "Xwayland") == 0) {
        for (;;) {
            ::pause();
        }
    }
    run_fake_host_tests();
    run_desktop_tests();
    run_window_frame_tests();
    run_icon_tests();
    run_pbm_tests();
    run_terminal_tests();
    run_pty_tests();
    run_wire_tests();
    run_remote_canvas_tests();
    run_font_tests();
    run_keymap_tests();
    run_evdev_input_tests();
    run_fbdev_device_tests();
    run_compositor_server_tests();
    run_compositor_shm_tests();
    run_compositor_surface_tests();
    run_compositor_shell_tests();
    run_compositor_seat_tests();
    run_compositor_desk_tests();
    run_compositor_cadence_tests();
    run_compositor_xwayland_tests();
    return vesper::testing::failure_count() == 0 ? 0 : 1;
}
