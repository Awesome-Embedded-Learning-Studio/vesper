# The compositor lane (the roadmap's C series, documents/desktop-roadmap.md):
# vesper grows into a Wayland server beside its host-table output. The server
# speaks the system libwayland and generates its protocol code with the
# system wayland-scanner from the system's protocol XML -- the same line the
# eyeball lane above draws: nothing vendored, third_party grows by nothing.
#
# Unlike the eyeball lanes this one is mainline: the C-series judgments run
# headless inside vesper.tests, so missing pieces are configure errors rather
# than quietly skipped lanes.
find_package(PkgConfig REQUIRED)
pkg_check_modules(VESPER_WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server>=1.26)
pkg_check_modules(VESPER_WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client>=1.26)
pkg_check_modules(VESPER_XCB REQUIRED IMPORTED_TARGET xcb xcb-composite)
pkg_check_modules(VESPER_WAYLAND_PROTOCOLS REQUIRED wayland-protocols>=1.30)
pkg_get_variable(VESPER_WAYLAND_PROTOCOLS_DATADIR wayland-protocols pkgdatadir)
# Some distributions keep the scanner beside libwayland in sbin, which
# find_program does not search by default, so the package prefix hints it.
find_program(VESPER_WAYLAND_SCANNER REQUIRED NAMES wayland-scanner
             HINTS "${VESPER_WAYLAND_SERVER_PREFIX}/bin"
                   "${VESPER_WAYLAND_SERVER_PREFIX}/../sbin"
                   "${VESPER_WAYLAND_SERVER_PREFIX}/sbin")

set(VESPER_COMPOSITOR_GEN ${CMAKE_BINARY_DIR}/generated/compositor)
set(VESPER_COMPOSITOR_XDG_XML
    ${VESPER_WAYLAND_PROTOCOLS_DATADIR}/stable/xdg-shell/xdg-shell.xml)
add_custom_command(
    OUTPUT ${VESPER_COMPOSITOR_GEN}/xdg-shell-protocol.c
           ${VESPER_COMPOSITOR_GEN}/xdg-shell-server-protocol.h
           ${VESPER_COMPOSITOR_GEN}/xdg-shell-client-protocol.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${VESPER_COMPOSITOR_GEN}
    COMMAND ${VESPER_WAYLAND_SCANNER} server-header ${VESPER_COMPOSITOR_XDG_XML}
            ${VESPER_COMPOSITOR_GEN}/xdg-shell-server-protocol.h
    COMMAND ${VESPER_WAYLAND_SCANNER} client-header ${VESPER_COMPOSITOR_XDG_XML}
            ${VESPER_COMPOSITOR_GEN}/xdg-shell-client-protocol.h
    COMMAND ${VESPER_WAYLAND_SCANNER} private-code ${VESPER_COMPOSITOR_XDG_XML}
            ${VESPER_COMPOSITOR_GEN}/xdg-shell-protocol.c
    DEPENDS ${VESPER_COMPOSITOR_XDG_XML}
    COMMENT "wayland-scanner 生成 xdg-shell 服务端与客户端代码"
    VERBATIM)
set_source_files_properties(${VESPER_COMPOSITOR_GEN}/xdg-shell-protocol.c
                            PROPERTIES LANGUAGE C)

# One protocol library serves both sides: the private code carries the
# interface tables the server and the client headers both name, so compiling
# it once keeps every symbol single while the compositor and the sparring
# test client each bring their own header. Generated code is a guest, not
# house code: SYSTEM keeps the check lanes from reading it as vesper's own.
add_library(vesper.wayland.protocol STATIC ${VESPER_COMPOSITOR_GEN}/xdg-shell-protocol.c)
target_include_directories(vesper.wayland.protocol SYSTEM INTERFACE ${VESPER_COMPOSITOR_GEN})

# The compositor library: protocol traffic in, ShellSurfaces out. It links
# neither ui nor host -- it is the guest-facing half the way session/wire is
# the child-facing half, and the desk decides where the surfaces land.
# _GNU_SOURCE buys mremap for the shm pool's resize word.
add_library(vesper.compositor STATIC src/compositor/server.cpp src/compositor/shell.cpp
                                           src/compositor/xwayland.cpp
                                           src/compositor/shm.cpp src/compositor/seat.cpp src/compositor/surface.cpp)
target_include_directories(vesper.compositor PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(vesper.compositor PRIVATE _GNU_SOURCE)
target_link_libraries(vesper.compositor
    PUBLIC visor.core
    PUBLIC vesper.wayland.protocol
    PRIVATE PkgConfig::VESPER_WAYLAND_SERVER PkgConfig::VESPER_XCB)
