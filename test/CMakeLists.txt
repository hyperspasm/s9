add_executable(test_ring_buffer
    test_ring_buffer.c
    ${CMAKE_SOURCE_DIR}/src/ring_buffer.c
    ${CMAKE_SOURCE_DIR}/src/s9_log.c
)

target_compile_definitions(test_ring_buffer PRIVATE TESTING)

target_include_directories(test_ring_buffer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${SDL2_INCLUDE_DIRS}
)

target_link_directories(test_ring_buffer PRIVATE
    ${SDL2_LIBRARY_DIRS}
)

target_link_libraries(test_ring_buffer
    ${SDL2_LIBRARIES}
)

add_test(NAME ring_buffer COMMAND test_ring_buffer)

add_executable(test_osc_sampler
    test_osc_sampler.c
    ${CMAKE_SOURCE_DIR}/src/osc_sampler.c
    ${CMAKE_SOURCE_DIR}/src/s9_log.c
)

target_compile_definitions(test_osc_sampler PRIVATE TESTING)

target_include_directories(test_osc_sampler PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${SDL2_INCLUDE_DIRS}
)

target_link_directories(test_osc_sampler PRIVATE
    ${SDL2_LIBRARY_DIRS}
)

target_link_libraries(test_osc_sampler
    ${SDL2_LIBRARIES}
    m
)

add_test(NAME osc_sampler COMMAND test_osc_sampler)
