# Generates a header with the current git hash. Run at build time so the
# hash stays current without reconfiguring. Only rewrites the file when the
# content changes, to avoid needless recompiles.
#
# Inputs: SRC_DIR (repo root), OUT (path of header to write)

execute_process(
    COMMAND git -C "${SRC_DIR}" describe --always --dirty --exclude "*"
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GIT_RESULT
)

if(NOT GIT_RESULT EQUAL 0 OR GIT_HASH STREQUAL "")
    set(GIT_HASH "unknown")
endif()

set(CONTENT "#define S9_GIT_HASH \"${GIT_HASH}\"\n")

set(OLD_CONTENT "")
if(EXISTS "${OUT}")
    file(READ "${OUT}" OLD_CONTENT)
endif()

if(NOT OLD_CONTENT STREQUAL CONTENT)
    file(WRITE "${OUT}" "${CONTENT}")
endif()
