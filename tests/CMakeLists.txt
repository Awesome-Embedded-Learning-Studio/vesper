# Single verification entry: every case links into vesper.tests, CTest sees one
# executable. Tests include the desktop layer the way a real consumer does,
# "desktop/...", "host/..." and "fonts/..." through vesper.ui's include path.
add_executable(vesper.tests main.cpp fake_host_tests.cpp desktop_tests.cpp
                            window_frame_tests.cpp icon_tests.cpp pbm_tests.cpp
                            terminal_tests.cpp pty_tests.cpp font_tests.cpp
                            keymap_tests.cpp evdev_input_tests.cpp fbdev_device_tests.cpp)
target_link_libraries(vesper.tests PRIVATE vesper.ui)

add_test(NAME vesper.tests COMMAND vesper.tests)
