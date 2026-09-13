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
    ${CMAKE_SOURCE_DIR}/src/pll.c
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

# DSP tests use the greatest harness and the golden tables from validation/
set(DSP_TEST_INCLUDES
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/greatest
    ${CMAKE_CURRENT_SOURCE_DIR}/golden
)

add_executable(test_fft_analyzer test_fft_analyzer.c
    ${CMAKE_SOURCE_DIR}/src/fft_analyzer.c
    ${CMAKE_SOURCE_DIR}/src/window_function.c
    ${CMAKE_SOURCE_DIR}/src/ring_buffer.c
    ${CMAKE_SOURCE_DIR}/src/s9_dsp.c
    ${CMAKE_SOURCE_DIR}/src/s9_log.c
)
target_include_directories(test_fft_analyzer PRIVATE ${DSP_TEST_INCLUDES})
target_link_libraries(test_fft_analyzer PkgConfig::SDL2 PkgConfig::FFTW3F m)
add_test(NAME fft_analyzer COMMAND test_fft_analyzer)

add_executable(test_period test_period.c
    ${CMAKE_SOURCE_DIR}/src/period.c
    ${CMAKE_SOURCE_DIR}/src/fft_analyzer.c
    ${CMAKE_SOURCE_DIR}/src/window_function.c
    ${CMAKE_SOURCE_DIR}/src/ring_buffer.c
    ${CMAKE_SOURCE_DIR}/src/s9_dsp.c
    ${CMAKE_SOURCE_DIR}/src/s9_log.c
)
target_include_directories(test_period PRIVATE ${DSP_TEST_INCLUDES})
target_link_libraries(test_period PkgConfig::SDL2 PkgConfig::FFTW3F m)
add_test(NAME period COMMAND test_period)

add_executable(test_harmonic_readout test_harmonic_readout.c
    ${CMAKE_SOURCE_DIR}/src/harmonic_readout.c
    ${CMAKE_SOURCE_DIR}/src/fft_analyzer.c
    ${CMAKE_SOURCE_DIR}/src/window_function.c
    ${CMAKE_SOURCE_DIR}/src/ring_buffer.c
    ${CMAKE_SOURCE_DIR}/src/s9_dsp.c
    ${CMAKE_SOURCE_DIR}/src/s9_log.c
)
target_include_directories(test_harmonic_readout PRIVATE ${DSP_TEST_INCLUDES})
target_link_libraries(test_harmonic_readout PkgConfig::SDL2 PkgConfig::FFTW3F m)
add_test(NAME harmonic_readout COMMAND test_harmonic_readout)

add_executable(test_pll test_pll.c ${CMAKE_SOURCE_DIR}/src/pll.c)
target_include_directories(test_pll PRIVATE ${DSP_TEST_INCLUDES})
target_link_libraries(test_pll m)
add_test(NAME pll COMMAND test_pll)
add_executable(test_note test_note.c ${CMAKE_SOURCE_DIR}/src/note.c)
target_include_directories(test_note PRIVATE ${DSP_TEST_INCLUDES})
target_link_libraries(test_note m)
add_test(NAME note COMMAND test_note)

add_executable(test_fundamental test_fundamental.c
    ${CMAKE_SOURCE_DIR}/src/fundamental.c
    ${CMAKE_SOURCE_DIR}/src/period.c
    ${CMAKE_SOURCE_DIR}/src/fft_analyzer.c
    ${CMAKE_SOURCE_DIR}/src/window_function.c
    ${CMAKE_SOURCE_DIR}/src/ring_buffer.c
    ${CMAKE_SOURCE_DIR}/src/s9_dsp.c
    ${CMAKE_SOURCE_DIR}/src/s9_log.c
)
target_include_directories(test_fundamental PRIVATE ${DSP_TEST_INCLUDES})
target_link_libraries(test_fundamental PkgConfig::SDL2 PkgConfig::FFTW3F m)
add_test(NAME fundamental COMMAND test_fundamental)

add_executable(test_harmonic_reader test_harmonic_reader.c
    ${CMAKE_SOURCE_DIR}/src/harmonic_reader.c
    ${CMAKE_SOURCE_DIR}/src/harmonic_readout.c
    ${CMAKE_SOURCE_DIR}/src/fft_analyzer.c
    ${CMAKE_SOURCE_DIR}/src/window_function.c
    ${CMAKE_SOURCE_DIR}/src/ring_buffer.c
    ${CMAKE_SOURCE_DIR}/src/s9_dsp.c
    ${CMAKE_SOURCE_DIR}/src/s9_log.c
)
target_include_directories(test_harmonic_reader PRIVATE ${DSP_TEST_INCLUDES})
target_link_libraries(test_harmonic_reader PkgConfig::SDL2 PkgConfig::FFTW3F m)
add_test(NAME harmonic_reader COMMAND test_harmonic_reader)

add_executable(test_periodic test_periodic.c
    ${CMAKE_SOURCE_DIR}/src/periodic.c
    ${CMAKE_SOURCE_DIR}/src/oscilloscope.c
    ${CMAKE_SOURCE_DIR}/src/osc_sampler.c
    ${CMAKE_SOURCE_DIR}/src/pll.c
    ${CMAKE_SOURCE_DIR}/src/scope_display.c
    ${CMAKE_SOURCE_DIR}/src/graticule.c
    ${CMAKE_SOURCE_DIR}/src/harmonics.c
    ${CMAKE_SOURCE_DIR}/src/label.c
    ${CMAKE_SOURCE_DIR}/src/note.c
    ${CMAKE_SOURCE_DIR}/src/note_strip.c
    ${CMAKE_SOURCE_DIR}/src/fundamental.c
    ${CMAKE_SOURCE_DIR}/src/harmonic_reader.c
    ${CMAKE_SOURCE_DIR}/src/harmonic_readout.c
    ${CMAKE_SOURCE_DIR}/src/period.c
    ${CMAKE_SOURCE_DIR}/src/fft_analyzer.c
    ${CMAKE_SOURCE_DIR}/src/graph_axis.c
    ${CMAKE_SOURCE_DIR}/src/draw_sdl.c
    ${CMAKE_SOURCE_DIR}/src/s9_font.c
    ${CMAKE_SOURCE_DIR}/src/window_function.c
    ${CMAKE_SOURCE_DIR}/src/ring_buffer.c
    ${CMAKE_SOURCE_DIR}/src/s9_dsp.c
    ${CMAKE_SOURCE_DIR}/src/s9_log.c
)
target_include_directories(test_periodic PRIVATE ${DSP_TEST_INCLUDES})
target_link_libraries(test_periodic PkgConfig::SDL2 PkgConfig::SDL2_TTF PkgConfig::FFTW3F m)
add_test(NAME periodic COMMAND test_periodic)

add_executable(test_www test_www.c)
target_link_libraries(test_www s9_www)
target_compile_definitions(test_www PRIVATE S9_WWW_SRC="${CMAKE_SOURCE_DIR}/www")
add_test(NAME www COMMAND test_www)

# Websocket hub: a loopback server driven by a Python client, through pn's venv
# when it exists. The script skips itself if the websockets package is missing.
add_executable(test_ws_server test_ws_server.c
    ${CMAKE_SOURCE_DIR}/src/s9_ws.c
    ${CMAKE_SOURCE_DIR}/src/s9_log.c
)
target_include_directories(test_ws_server PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_ws_server s9_www PkgConfig::LWS)
find_program(PN_VENV_PYTHON python3 PATHS ${CMAKE_SOURCE_DIR}/pn/venv/bin NO_DEFAULT_PATH)
if(NOT PN_VENV_PYTHON)
    find_program(PN_VENV_PYTHON python3)
endif()
if(PN_VENV_PYTHON)
    add_test(NAME ws COMMAND ${PN_VENV_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/test_ws.py
        $<TARGET_FILE:test_ws_server> ${CMAKE_SOURCE_DIR}/www)
    set_tests_properties(ws PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()

# The committed manifest must match what the schema tool produces
pn_add_manifest_test(NAME s9_manifest TOOL s9_schema EXPECTED ${CMAKE_SOURCE_DIR}/s9.manifest)
