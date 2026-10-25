cmake_minimum_required(VERSION 3.28)
project(vesper LANGUAGES C CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C++20 modules are unused; disabling the scan keeps compile_commands.json free
# of GCC-only flags so clang-tidy and clangd parse it cleanly.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# The visor borrow: VISOR_ROOT lookup, the borrowed targets and the X11 tier.
include(cmake/visor.cmake)

# The compositor lane (the roadmap's C series): vesper as a Wayland server.
# It comes after the visor borrow because the judgment lane links both.
include(cmake/compositor.cmake)

option(VESPER_SANITIZE "ASan+UBSan switch" OFF)
option(VESPER_TSAN "TSan switch" OFF)

# The two sanitizers are configure-time exclusive: their runtimes cannot load
# into one process, so the conflict fails here instead of at link time.
if(VESPER_SANITIZE AND VESPER_TSAN)
    message(FATAL_ERROR "VESPER_SANITIZE and VESPER_TSAN are exclusive: separate build dirs, one switch each")
endif()

add_compile_options(-fno-exceptions -fno-rtti -fno-threadsafe-statics -Wall -Wextra -Werror)

if(VESPER_SANITIZE)
    add_compile_options(-fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

if(VESPER_TSAN)
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
endif()

# The session layer: the private channel to windowed children, wire words
# plus the spawn. It sits under ui and beside host, linking only the core
# so both the desk and the standalone children can speak it.
add_library(vesper.session STATIC src/session/wire.cpp
                                    src/session/child_spawn.cpp)
target_include_directories(vesper.session PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vesper.session PUBLIC visor.core)

# The app lane (the roadmap's T series): first-party programs that are guests
# of the desk, not parts of it. It stands before ui because the desk's host
# wiring folds text through the table that moved with the terminal lane.
add_subdirectory(apps/terminal)

# The three layers: base decodes assets, ui carries the desktop modules, host
# wires the machine.
add_library(
    vesper.ui STATIC
    src/base/libpbm/pbm.cpp
    src/ui/desktop/desktop.cpp
    src/ui/icon/icon.cpp
    src/ui/launcher/launcher.cpp
    src/ui/shell_desk/shell_desk.cpp
    src/ui/wayland_canvas/wayland_canvas.cpp
    src/ui/window_frame/window_frame.cpp
    src/host/evdev_input.cpp
    src/host/fake_session_host.cpp
    src/host/fbdev_device.cpp
    src/ui/remote_canvas/remote_canvas.cpp)
# The asset headers live outside src as art, so assets/ joins the include
# path: fonts arrive as "fonts/...", icons as "icons/...".
target_include_directories(vesper.ui PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/assets)
# The desk's host pieces fold raw key numbers into text through the table
# that lives in the terminal lane (evdev_input, wayland_canvas). The fold
# straddles the desk/app seam this round; its long-term home is a visor
# upstreaming conversation, recorded in the roadmap's T-series section.
target_link_libraries(vesper.ui PUBLIC visor.ui visor.host vesper.session
                                vesper.compositor vesper.terminal)

enable_testing()
add_subdirectory(tests)

# The eyeball and VM lanes are wired here, not inside the visor borrow: the
# examples link vesper.ui, so they can only join after this project's own
# targets exist. The fbdev example needs no display server and builds
# everywhere Linux headers do; the X11 one gates itself inside.
add_subdirectory(examples)

# Optional tooling lanes: clang-format and clang-tidy over vesper sources.
include(cmake/checks.cmake)
