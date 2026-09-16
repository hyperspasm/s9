cmake_minimum_required(VERSION 3.16)

# Support standalone builds (when pn is its own project)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(pn VERSION 0.1.0 LANGUAGES C)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    add_compile_options(-Wall -Wextra)
    include(CTest)
endif()

# pn_add_manifest_test(NAME <test> TOOL <schema tool target> EXPECTED <committed manifest>)
# Adds a test that runs a schema tool and compares its manifest with a committed
# file. Both are compared in canonical form; see cmake/check_manifest.cmake.
set(PN_CHECK_MANIFEST_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_manifest.cmake CACHE INTERNAL "")
function(pn_add_manifest_test)
    cmake_parse_arguments(ARG "" "NAME;TOOL;EXPECTED" "" ${ARGN})
    add_test(NAME ${ARG_NAME}
        COMMAND ${CMAKE_COMMAND}
            -DTOOL=$<TARGET_FILE:${ARG_TOOL}>
            -DEXPECTED=${ARG_EXPECTED}
            -P ${PN_CHECK_MANIFEST_SCRIPT})
endfunction()

if(BUILD_TESTING)
    add_subdirectory(example)
    add_subdirectory(test)
endif()

add_library(pn STATIC
    src/pn_server.c
    src/pn_client.c
    src/pn_manifest.c
    src/pn_fnv.c
    src/pn_crc.c
    src/pn_cobs.c
    extern/nanopb/pb_common.c
    extern/nanopb/pb_encode.c
    extern/nanopb/pb_decode.c
    proto/pn.pb.c
)

target_include_directories(pn
    PUBLIC include
    PRIVATE extern/nanopb proto src
)

add_subdirectory(transport)
