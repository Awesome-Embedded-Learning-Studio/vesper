# The visor borrow: find the tree, then compile its sources straight from it.
# Nothing inside the visor tree is modified or copied. add_subdirectory is not
# an option on purpose -- visor's own CMakeLists resolves its include path
# against CMAKE_SOURCE_DIR and requires X11 unconditionally, both of which only
# hold while visor is the root project.

set(VISOR_ROOT "/home/charliechen/visor" CACHE PATH "visor source tree borrowed in place")
if(NOT EXISTS "${VISOR_ROOT}/include/visor/bindings/host.hpp")
    message(FATAL_ERROR "VISOR_ROOT (${VISOR_ROOT}) carries no include/visor: a wrong tree was pointed at")
endif()

add_library(visor.core INTERFACE)
target_include_directories(visor.core INTERFACE ${VISOR_ROOT}/include)

# The glob follows the live tree so a new visor source file joins the build by
# itself; visor runs its own checks in its own repo.
file(GLOB_RECURSE VESPER_VISOR_UI_SOURCES CONFIGURE_DEPENDS ${VISOR_ROOT}/src/ui/*.cpp)
add_library(visor.ui STATIC ${VESPER_VISOR_UI_SOURCES})
target_link_libraries(visor.ui PUBLIC visor.core)

# The host library carries the two pure bindings beside the table itself:
# the evdev word translator (keys, buttons, relative axes) and the cinux
# frame expansion (mono bits into XRGB8888 words). Both are plain logic
# with no system dependency, so they ride the core host target; the X11
# binding stays separate below because it links a real library.
add_library(visor.host STATIC
    ${VISOR_ROOT}/src/bindings/host.cpp
    ${VISOR_ROOT}/src/bindings/evdev/event_wire.cpp
    ${VISOR_ROOT}/src/bindings/cinux/frame_expand.cpp)
target_link_libraries(visor.host PUBLIC visor.core)

# The eyeball lane: an X11 window running the same scene the judgments render.
# It exists to be looked at, so it is skipped without X11 and carries no test.
# Only the borrowed library lives here; the example that links it is wired up
# by the root project after its own targets exist.
find_package(X11 QUIET)
if(X11_FOUND)
    add_library(visor.host.x11 STATIC
        ${VISOR_ROOT}/src/bindings/x11/event_wire.cpp
        ${VISOR_ROOT}/src/bindings/x11/x11_window_host.cpp)
    target_link_libraries(visor.host.x11 PUBLIC visor.core X11::X11)
else()
    message(STATUS "X11 not found: the eyeball example is skipped")
endif()
