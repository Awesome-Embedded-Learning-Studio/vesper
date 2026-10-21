# Optional developer tooling, mirroring the visor stance: each check target is
# created only when its tool is present, and version drift is accepted. The
# globs see vesper sources only -- borrowed visor code is checked in its own
# repo.

find_program(VESPER_CLANG_FORMAT NAMES clang-format)
find_program(VESPER_CLANG_TIDY NAMES clang-tidy)

file(GLOB_RECURSE VESPER_FORMAT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.hpp
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/assets/*.hpp
    ${CMAKE_SOURCE_DIR}/examples/*.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/*.cpp
    ${CMAKE_SOURCE_DIR}/examples/*.cpp)

if(VESPER_CLANG_TIDY)
    add_custom_target(vesper.check-tidy
        COMMAND ${VESPER_CLANG_TIDY} --quiet -p ${CMAKE_BINARY_DIR} ${VESPER_TIDY_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "clang-tidy check")
endif()
