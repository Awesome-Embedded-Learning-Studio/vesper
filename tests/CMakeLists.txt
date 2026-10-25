# Single verification entry: every case links into vesper.tests, CTest sees one
# executable. Tests include the layers the way a real consumer does, "ui/...",
# "host/...", "base/..." and the asset headers, through vesper.ui's include
# path. Folders mirror the source tree.
add_executable(vesper.tests main.cpp base/pbm_tests.cpp
                            ui/desktop_tests.cpp ui/window_frame_tests.cpp
                            ui/icon_tests.cpp ui/launcher_tests.cpp
                            apps/terminal_tests.cpp apps/pty_tests.cpp
                            apps/keymap_tests.cpp host/evdev_input_tests.cpp
                            host/fake_host_tests.cpp host/fbdev_device_tests.cpp
                            session/wire_tests.cpp session/remote_canvas_tests.cpp
                            compositor/server_tests.cpp compositor/sparring_client.cpp
                            compositor/shm_tests.cpp compositor/surface_tests.cpp
                            compositor/shell_tests.cpp compositor/seat_tests.cpp
                            compositor/shell_desk_tests.cpp
                            compositor/cadence_tests.cpp
                            compositor/xwayland_tests.cpp
                            compositor/guest_spawn_tests.cpp
                            apps/terminal_app_tests.cpp
                            assets/font_tests.cpp)
# The harness headers (cases.hpp, testing.hpp) stay at the tests root, so the
# subfolder cases reach them through this include path.
target_include_directories(vesper.tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# The compositor lane rides the system wayland client beside the server: the
# sparring partner speaks wl_shm and xdg words of its own, in process.
target_link_libraries(vesper.tests PRIVATE vesper.ui vesper.compositor vesper.terminal
                                        PkgConfig::VESPER_WAYLAND_CLIENT
                                        visor.host.posix)
# The terminal app judgment births the real program; the generator hands
# its built path over as a word.
target_compile_definitions(vesper.tests PRIVATE
    VESPER_TERMINAL_PATH="$<TARGET_FILE:vesper_terminal>")

add_test(NAME vesper.tests COMMAND vesper.tests)
