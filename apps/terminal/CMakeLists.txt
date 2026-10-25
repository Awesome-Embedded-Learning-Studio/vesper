# The terminal lane (the roadmap's T series): the terminal as its own program,
# out of the desk's library. The static library keeps the pieces any terminal
# host needs -- the grid, the pty pump, the fork and the text fold -- linking
# only visor, so a terminal binary drags neither the compositor server nor the
# private session channel into its link.
add_library(vesper.terminal STATIC
    host/keymap.cpp
    host/pty.cpp
    ui/pty_source/pty_source.cpp
    ui/terminal/terminal.cpp)
# The lane's tree spells its includes the way src/ always has ("ui/terminal/...",
# "host/..."), and the fonts ride the shared assets root every terminal
# consumer already reaches them through.
target_include_directories(vesper.terminal PUBLIC ${CMAKE_SOURCE_DIR}/apps/terminal
                                        PUBLIC ${CMAKE_SOURCE_DIR}/assets)
# visor.host rides along for the HostTable ABI the fold's wrapper speaks.
target_link_libraries(vesper.terminal PUBLIC visor.ui visor.host visor.core)
