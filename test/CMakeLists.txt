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

add_executable(test_resampler
    test_resampler.c
    ${CMAKE_SOURCE_DIR}/src/resampler.c
    ${CMAKE_SOURCE_DIR}/src/s9_log.c
)

target_compile_definitions(test_resampler PRIVATE TESTING)

target_include_directories(test_resampler PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${SDL2_INCLUDE_DIRS}
)

target_link_directories(test_resampler PRIVATE
    ${SDL2_LIBRARY_DIRS}
)

target_link_libraries(test_resampler
    ${SDL2_LIBRARIES}
    m
)

add_test(NAME resampler COMMAND test_resampler)

# The committed manifest must match what the schema tool produces
pn_add_manifest_test(NAME s9_manifest TOOL s9_schema EXPECTED ${CMAKE_SOURCE_DIR}/s9.manifest)
