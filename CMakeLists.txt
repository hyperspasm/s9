cmake_minimum_required(VERSION 3.16)
project(s9 VERSION 1.0.0 LANGUAGES C)

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

# Tests
include(CTest)

# pn library (parameter network)
add_subdirectory(pn)

# Collect source files
file(GLOB SOURCES src/*.c)

# Main executable
add_executable(s9 ${SOURCES})

# Link using imported targets (handles includes, library dirs, and dependencies automatically)
target_link_libraries(s9
    PkgConfig::SDL2
    PkgConfig::SDL2_TTF
    PkgConfig::FFTW3F
    pn
    m
)

add_subdirectory(test)
