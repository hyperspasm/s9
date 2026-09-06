# Compare a schema tool's manifest with a committed one.
#
#   cmake -DTOOL=<schema tool executable> -DEXPECTED=<committed manifest> -P check_manifest.cmake
#
# Both manifests are reduced to canonical form (comments and blank lines
# removed, whitespace collapsed) before comparison, so the committed file may
# carry comments and its own whitespace. When the committed file declares a
# hash in its header, that hash must match the tool's as well.

if(NOT TOOL OR NOT EXPECTED)
    message(FATAL_ERROR "usage: cmake -DTOOL=<executable> -DEXPECTED=<manifest> -P check_manifest.cmake")
endif()

function(canonicalize text out_var)
    string(REGEX REPLACE "#[^\n]*" "" text "${text}")
    string(REGEX REPLACE "[ \t\r]+" " " text "${text}")
    string(REGEX REPLACE "\n " "\n" text "${text}")
    string(REGEX REPLACE " \n" "\n" text "${text}")
    string(REGEX REPLACE "^ " "" text "${text}")
    string(REGEX REPLACE " $" "" text "${text}")
    string(REGEX REPLACE "\n+" "\n" text "${text}")
    string(REGEX REPLACE "^\n" "" text "${text}")
    if(NOT text STREQUAL "" AND NOT text MATCHES "\n$")
        string(APPEND text "\n")
    endif()
    set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

# The hash declared in a manifest header, or "" if there is none
function(declared_hash text out_var)
    if(text MATCHES "fnv1a32=([0-9a-f]+)")
        set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
    else()
        set(${out_var} "" PARENT_SCOPE)
    endif()
endfunction()

execute_process(
    COMMAND "${TOOL}"
    OUTPUT_VARIABLE actual
    ERROR_VARIABLE tool_error
    RESULT_VARIABLE tool_result)
if(NOT tool_result EQUAL 0)
    message(FATAL_ERROR "${TOOL} failed (${tool_result}):\n${tool_error}")
endif()

file(READ "${EXPECTED}" expected)

canonicalize("${actual}" actual_canonical)
canonicalize("${expected}" expected_canonical)
declared_hash("${actual}" actual_hash)
declared_hash("${expected}" expected_hash)

set(regenerate "the schema changed; regenerate with:\n  ${TOOL} > ${EXPECTED}\n")

if(NOT actual_canonical STREQUAL expected_canonical)
    get_filename_component(name "${EXPECTED}" NAME)
    set(actual_file "${CMAKE_CURRENT_BINARY_DIR}/${name}.actual")
    set(expected_file "${CMAKE_CURRENT_BINARY_DIR}/${name}.expected")
    file(WRITE "${actual_file}" "${actual_canonical}")
    file(WRITE "${expected_file}" "${expected_canonical}")
    set(diff_text "")
    find_program(DIFF diff)
    if(DIFF)
        execute_process(COMMAND "${DIFF}" -u "${expected_file}" "${actual_file}" OUTPUT_VARIABLE diff_text)
    endif()
    message(FATAL_ERROR "manifest mismatch: ${EXPECTED}\n${regenerate}${diff_text}")
endif()

if(expected_hash AND NOT expected_hash STREQUAL actual_hash)
    message(FATAL_ERROR "manifest hash mismatch: ${EXPECTED} declares ${expected_hash}, tool reports ${actual_hash}\n${regenerate}")
endif()
