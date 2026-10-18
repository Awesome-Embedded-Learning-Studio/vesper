# The eyeball lane: built when X11 is present, run by hand, never by CTest.
add_executable(x11_desktop x11_desktop.cpp)
target_link_libraries(x11_desktop PRIVATE vesper.ui visor.host.x11)
