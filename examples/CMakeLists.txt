# The eyeball lane: built when X11 is present, run by hand, never by CTest.
if(X11_FOUND)
    add_executable(x11_desktop x11_desktop.cpp)
    target_link_libraries(x11_desktop PRIVATE vesper.ui visor.host.x11)
else()
    message(STATUS "X11 not found: the eyeball example is skipped")
endif()

# The Wayland eyeball lane: the X11 desk's same scene on the second real host.
# Same treatment -- built when the pieces are there, run by hand, never by
# CTest.
if(VESPER_WAYLAND_FOUND)
    add_executable(wayland_desktop wayland_desktop.cpp)
    target_link_libraries(wayland_desktop PRIVATE vesper.ui visor.host.wayland)
else()
    message(STATUS "wayland pieces not found: the wayland eyeball example is skipped")
endif()

# The D6a minimal child: the bouncing square speaking the private channel,
# a standalone binary with no desk and no canvas -- run by hand, spawned by
# the desk's example lane.
add_executable(d6_block_child d6_block_child.cpp)
target_link_libraries(d6_block_child PRIVATE vesper.session)

# The VM lane: the bare-framebuffer desk the initramfs runs under QEMU. It
# needs no display server, only Linux fbdev/evdev headers, so it builds on
# every Linux host even where no /dev/fb0 exists to run it against.
add_executable(fbdev_desktop fbdev_desktop.cpp)
target_link_libraries(fbdev_desktop PRIVATE vesper.ui)
