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
pkg_check_modules(CUNIT REQUIRED cunit)

file(GLOB TEST_SOURCES test/*.c)

# Source files that need to be compiled with -DTESTING
set(TEST_SUPPORT_SOURCES
    src/ring_buffer.c
    src/s9_log.c
)

add_executable(s9_test
    ${TEST_SOURCES}
    ${TEST_SUPPORT_SOURCES}
)

target_compile_definitions(s9_test PRIVATE TESTING)

target_include_directories(s9_test PRIVATE
    src
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_TTF_INCLUDE_DIRS}
    ${FFTW3F_INCLUDE_DIRS}
    ${CUNIT_INCLUDE_DIRS}
)

target_link_directories(s9_test PRIVATE
    ${SDL2_LIBRARY_DIRS}
    ${SDL2_TTF_LIBRARY_DIRS}
    ${FFTW3F_LIBRARY_DIRS}
    ${CUNIT_LIBRARY_DIRS}
)

target_link_libraries(s9_test
    ${CUNIT_LIBRARIES}
    ${SDL2_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
    ${FFTW3F_LIBRARIES}
    m
)

# Enable CTest
enable_testing()
add_test(NAME s9_test COMMAND s9_test)
