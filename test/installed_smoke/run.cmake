# Installed-config smoke driver (script mode, Batch G P1.5): installs
# the already-built imprint tree into a staging prefix, configures +
# builds + runs the find_package consumer against it. Args:
#   -DSRC=<repo root> -DBIN=<imprint build dir>
# Single-config generators (the CI linux-desktop shape); a
# multi-config host builds a per-config variant when one appears.
set(STAGE ${BIN}/installed_smoke_stage)
set(BLD ${BIN}/installed_smoke_build)
file(REMOVE_RECURSE ${STAGE} ${BLD})

# the build tree may have been configured incrementally (a verify run
# only builds the battery target); the install needs every library
execute_process(
    COMMAND ${CMAKE_COMMAND} --build ${BIN}
    RESULT_VARIABLE r)
if (r)
    message(FATAL_ERROR "imprint build failed (${r})")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} --install ${BIN} --prefix ${STAGE}
    RESULT_VARIABLE r)
if (r)
    message(FATAL_ERROR "cmake --install failed (${r})")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND}
        -S ${SRC}/test/installed_smoke
        -B ${BLD}
        -DIMPRINT_STAGING_PREFIX=${STAGE}
    RESULT_VARIABLE r)
if (r)
    message(FATAL_ERROR "smoke configure failed (${r})")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} --build ${BLD}
    RESULT_VARIABLE r)
if (r)
    message(FATAL_ERROR "smoke build failed (${r})")
endif()

execute_process(
    COMMAND ${BLD}/bin/installed_smoke
    RESULT_VARIABLE r)
if (r)
    message(FATAL_ERROR "smoke run failed (${r})")
endif()

message("installed smoke: find_package consumer ran green")
