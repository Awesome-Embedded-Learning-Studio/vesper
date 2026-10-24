# Single verification entry: every case links into vesper.tests, CTest sees one
# executable. Tests include the layers the way a real consumer does, "ui/...",
# "host/...", "base/..." and the asset headers, through vesper.ui's include
# path. Folders mirror the source tree.
add_executable(vesper.tests main.cpp base/pbm_tests.cpp
                            ui/desktop_tests.cpp ui/window_frame_tests.cpp
                            ui/icon_tests.cpp ui/terminal_tests.cpp ui/pty_tests.cpp
                            host/keymap_tests.cpp host/evdev_input_tests.cpp
                            host/fake_host_tests.cpp host/fbdev_device_tests.cpp
                            session/wire_tests.cpp session/remote_canvas_tests.cpp
                            assets/font_tests.cpp)
# The harness headers (cases.hpp, testing.hpp) stay at the tests root, so the
# subfolder cases reach them through this include path.
target_include_directories(vesper.tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vesper.tests PRIVATE vesper.ui)

add_test(NAME vesper.tests COMMAND vesper.tests)
