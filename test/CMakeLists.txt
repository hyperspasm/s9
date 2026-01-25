pkg_check_modules(CUNIT REQUIRED cunit)

file(GLOB TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.c)

# Source files that need to be compiled with -DTESTING
set(TEST_SUPPORT_SOURCES
    ${CMAKE_SOURCE_DIR}/src/ring_buffer.c
    ${CMAKE_SOURCE_DIR}/src/s9_log.c
)

add_executable(s9_test
    ${TEST_SOURCES}
    ${TEST_SUPPORT_SOURCES}
)

target_compile_definitions(s9_test PRIVATE TESTING)

target_include_directories(s9_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
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

add_test(NAME s9_test COMMAND s9_test)
