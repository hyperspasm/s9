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

# Find dependencies using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)
pkg_check_modules(FFTW3F REQUIRED fftw3f)

# Collect source files
file(GLOB SOURCES src/*.c)

# Main executable
add_executable(s9 ${SOURCES})

target_include_directories(s9 PRIVATE
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_TTF_INCLUDE_DIRS}
    ${FFTW3F_INCLUDE_DIRS}
)

target_link_directories(s9 PRIVATE
    ${SDL2_LIBRARY_DIRS}
    ${SDL2_TTF_LIBRARY_DIRS}
    ${FFTW3F_LIBRARY_DIRS}
)

target_link_libraries(s9
    ${SDL2_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
    ${FFTW3F_LIBRARIES}
    m
)

# Tests
include(CTest)
add_subdirectory(test)
