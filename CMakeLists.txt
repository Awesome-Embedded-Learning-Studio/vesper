cmake_minimum_required(VERSION 3.28)
project(vesper LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C++20 modules are unused; disabling the scan keeps compile_commands.json free
# of GCC-only flags so clang-tidy and clangd parse it cleanly.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# The visor borrow: VISOR_ROOT lookup, the borrowed targets and the X11 tier.
include(cmake/visor.cmake)

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

# The desktop layer: session double, desktop root, the window frame and icons.
add_library(
    vesper.ui STATIC
    src/assets/pbm.cpp
    src/desktop/desktop.cpp
    src/desktop/icon.cpp
    src/desktop/pty_source.cpp
    src/desktop/terminal.cpp
    src/desktop/window_frame.cpp
    src/host/fake_session_host.cpp
    src/host/pty.cpp)
target_include_directories(vesper.ui PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vesper.ui PUBLIC visor.ui visor.host)

enable_testing()
add_subdirectory(tests)

# The eyeball lane is wired here, not inside the visor borrow: the example
# links vesper.ui, so it can only join after this project's own targets exist.
if(X11_FOUND)
    add_subdirectory(examples)
endif()

# Optional tooling lanes: clang-format and clang-tidy over vesper sources.
include(cmake/checks.cmake)
