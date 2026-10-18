cmake_minimum_required(VERSION 3.28)
project(vesper LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C++20 modules are unused; disabling the scan keeps compile_commands.json free
# of GCC-only flags so clang-tidy and clangd parse it cleanly.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# The desktop borrows the visor tree in place: this cache variable points at it,
# the targets below compile its sources straight from there, and nothing inside
# the visor tree is modified or copied. add_subdirectory is not an option on
# purpose -- visor's own CMakeLists resolves its include path against
# CMAKE_SOURCE_DIR and requires X11 unconditionally, both of which only hold
# while visor is the root project.
set(VISOR_ROOT "/home/charliechen/visor" CACHE PATH "visor source tree borrowed in place")
if(NOT EXISTS "${VISOR_ROOT}/include/visor/bindings/host.hpp")
    message(FATAL_ERROR "VISOR_ROOT (${VISOR_ROOT}) carries no include/visor: a wrong tree was pointed at")
endif()

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

# Optional developer tooling, mirroring the visor stance: each check target is
# created only when its tool is present, and version drift is accepted.
find_program(VESPER_CLANG_FORMAT NAMES clang-format)
find_program(VESPER_CLANG_TIDY NAMES clang-tidy)

# Borrowed visor targets. The glob follows the live tree so a new visor source
# file joins the build by itself; visor runs its own checks in its own repo, so
# the format and tidy lanes below only ever look at vesper sources.
add_library(visor.core INTERFACE)
target_include_directories(visor.core INTERFACE ${VISOR_ROOT}/include)

file(GLOB_RECURSE VESPER_VISOR_UI_SOURCES CONFIGURE_DEPENDS ${VISOR_ROOT}/src/ui/*.cpp)
add_library(visor.ui STATIC ${VESPER_VISOR_UI_SOURCES})
target_link_libraries(visor.ui PUBLIC visor.core)

add_library(visor.host STATIC ${VISOR_ROOT}/src/bindings/host.cpp)
target_link_libraries(visor.host PUBLIC visor.core)

# The desktop layer: session double and the desktop root.
add_library(vesper.ui STATIC src/desktop/desktop.cpp src/host/fake_session_host.cpp)
target_include_directories(vesper.ui PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vesper.ui PUBLIC visor.ui visor.host)

enable_testing()
add_subdirectory(tests)

file(GLOB_RECURSE VESPER_FORMAT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.hpp
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/tests/*.hpp
    ${CMAKE_SOURCE_DIR}/tests/*.cpp)

if(VESPER_CLANG_FORMAT)
    add_custom_target(vesper.check-format
        COMMAND ${VESPER_CLANG_FORMAT} --dry-run --Werror ${VESPER_FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "clang-format check")
endif()

file(GLOB_RECURSE VESPER_TIDY_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/tests/*.cpp)

if(VESPER_CLANG_TIDY)
    add_custom_target(vesper.check-tidy
        COMMAND ${VESPER_CLANG_TIDY} --quiet -p ${CMAKE_BINARY_DIR} ${VESPER_TIDY_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "clang-tidy check")
endif()
