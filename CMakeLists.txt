cmake_minimum_required(VERSION 3.16)
project(s9 VERSION 0.1.0 LANGUAGES C)

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
pkg_check_modules(FFTW3F REQUIRED IMPORTED_TARGET fftw3f)
pkg_check_modules(LWS REQUIRED IMPORTED_TARGET libwebsockets)

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
target_compile_definitions(s9 PRIVATE S9_VERSION="${PROJECT_VERSION}")

# Link using imported targets (handles includes, library dirs, and dependencies automatically)
target_link_libraries(s9
    PkgConfig::SDL2
    PkgConfig::SDL2_TTF
    PkgConfig::FFTW3F
    PkgConfig::LWS
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

add_subdirectory(test)
