cmake_minimum_required(VERSION 3.16)

# The version comes from git: the last tag of the form vX.Y.Z; a commit past
# it adds the count and the hash (0.0.1+3.g9734b76), which apt orders after
# the tag and before the next; uncommitted changes add +dirty. Read when
# CMake configures, which a commit triggers again; a new tag needs
# `cmake -S . -B build`. Without git, 0.0.0.
execute_process(
    COMMAND git describe --tags --match "v*" --dirty
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE S9_DESCRIBE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE S9_DESCRIBE_RESULT
)
if(S9_DESCRIBE_RESULT EQUAL 0 AND S9_DESCRIBE MATCHES "^v([0-9]+\\.[0-9]+\\.[0-9]+)(-([0-9]+)-g([0-9a-f]+))?(-dirty)?$")
    set(S9_VERSION_NUMBER ${CMAKE_MATCH_1})
    set(S9_VERSION ${CMAKE_MATCH_1})
    if(CMAKE_MATCH_3)
        string(APPEND S9_VERSION "+${CMAKE_MATCH_3}.g${CMAKE_MATCH_4}")
    endif()
    if(CMAKE_MATCH_5)
        string(APPEND S9_VERSION "+dirty")
    endif()
else()
    set(S9_VERSION_NUMBER 0.0.0)
    set(S9_VERSION 0.0.0)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git/logs/HEAD)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS .git/logs/HEAD)
endif()

project(s9 VERSION ${S9_VERSION_NUMBER} LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
add_compile_options(-Wall -Wextra)

# Release build optimization
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_C_FLAGS_DEBUG "-g -O0")

# Find dependencies using pkg-config (with IMPORTED_TARGET for proper dependency management)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
pkg_check_modules(LWS REQUIRED IMPORTED_TARGET libwebsockets)

# PFFFT, the FFT, compiled in from its one source file (see extern/pffft/pffft.h for its license)
add_library(pffft STATIC extern/pffft/pffft.c)
target_include_directories(pffft PUBLIC extern/pffft)
target_compile_options(pffft PRIVATE -w)   # vendored as is

# Tests
include(CTest)

# pn library (parameter network)
add_subdirectory(pn)

# Collect source files (s9_www.c belongs to the s9_www library below)
file(GLOB SOURCES src/*.c)
list(REMOVE_ITEM SOURCES ${CMAKE_SOURCE_DIR}/src/s9_www.c)

# Web interface files, embedded as C arrays; regenerated when a file under www/ changes
file(GLOB_RECURSE WWW_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/www/*)
set(WWW_EMBEDDED ${CMAKE_BINARY_DIR}/generated/s9_www_files.c)
add_custom_command(
    OUTPUT ${WWW_EMBEDDED}
    COMMAND ${CMAKE_COMMAND}
        -DSRC_DIR=${CMAKE_SOURCE_DIR}/www
        -DOUT=${WWW_EMBEDDED}
        -P ${CMAKE_SOURCE_DIR}/cmake/embed_www.cmake
    DEPENDS ${WWW_FILES} ${CMAKE_SOURCE_DIR}/cmake/embed_www.cmake
    COMMENT "Embedding www/"
    VERBATIM
)
add_library(s9_www STATIC src/s9_www.c ${WWW_EMBEDDED})
target_include_directories(s9_www PUBLIC src)

# Main executable
add_executable(s9 ${SOURCES})

# Version info: semantic version from project(), git hash generated at build time
set(GIT_VERSION_HEADER ${CMAKE_BINARY_DIR}/generated/s9_git_version.h)
add_custom_target(s9_git_version
    COMMAND ${CMAKE_COMMAND}
        -DSRC_DIR=${CMAKE_SOURCE_DIR}
        -DOUT=${GIT_VERSION_HEADER}
        -P ${CMAKE_SOURCE_DIR}/cmake/git_version.cmake
    BYPRODUCTS ${GIT_VERSION_HEADER}
    COMMENT "Updating git version header"
    VERBATIM
)
add_dependencies(s9 s9_git_version)
target_include_directories(s9 PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_compile_definitions(s9 PRIVATE S9_VERSION="${S9_VERSION}")

# Link using imported targets (handles includes, library dirs, and dependencies automatically)
target_link_libraries(s9
    PkgConfig::SDL2
    PkgConfig::SDL2_TTF
    PkgConfig::LWS
    pffft
    pn
    s9_www
    m
)

# Schema tool: the parameter tree compiled without its bindings, linking only pn.
# Writes the manifest that s9.manifest holds; test/ checks the two agree.
add_executable(s9_schema tools/s9_schema.c src/s9_params.c)
target_compile_definitions(s9_schema PRIVATE PN_SCHEMA_ONLY)
target_include_directories(s9_schema PRIVATE src)
target_link_libraries(s9_schema pn)

# Readout benchmark: the spectrogram's readout timed at the display's defaults, for sizing a machine
add_executable(cq_bench tools/cq_bench.c src/constant_q.c src/fft.c src/window_function.c src/s9_log.c)
target_include_directories(cq_bench PRIVATE src)
target_link_libraries(cq_bench PkgConfig::SDL2 pffft m)

add_subdirectory(test)

# The package: the program, its documentation and its copyright notices,
# with the runtime libraries it links found by dpkg-shlibdeps, as a Debian
# package for the machine it is built on (cmake --build build --target package).
install(TARGETS s9 RUNTIME DESTINATION bin)
install(FILES README.md packaging/copyright DESTINATION share/doc/s9)

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME s9)
set(CPACK_PACKAGE_VERSION ${S9_VERSION})
set(CPACK_PACKAGE_CONTACT "Shea Ako <commits@hyperspasm.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "real-time audio visualizer")
set(CPACK_PACKAGE_DESCRIPTION "An oscilloscope, a stereo lissajous display, a spectrum analyzer, VU meters, a periodic analyzer with harmonics and waveform views, and a scrolling spectrogram, drawn from a live audio input. Runs on the console through SDL's KMSDRM driver as well as on a desktop, and serves its parameters and a web interface over a websocket.")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)   # 755, whatever the umask
set(CPACK_STRIP_FILES ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION sound)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
include(CPack)
