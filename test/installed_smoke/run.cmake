# Installed-config smoke driver (script mode, Batch G P1.5): installs
# the already-built imprint tree into a staging prefix, configures +
# builds + runs the find_package consumer against it. Args:
#   -DSRC=<repo root> -DBIN=<imprint build dir>
# Single-config generators (the CI linux-desktop shape); a
# multi-config host builds a per-config variant when one appears.
set(STAGE ${BIN}/installed_smoke_stage)
set(BLD ${BIN}/installed_smoke_build)
file(REMOVE_RECURSE ${STAGE} ${BLD})
find_program(PKG_CONFIG_EXECUTABLE pkg-config)

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

# X-14: the package hygiene the audit measured — the shared kernel and
# the C ABI carry a SOVERSION (an in-place upgrade must not silently
# swap the library under a linked binary), the license ships, and the
# pkg-config recipe resolves against the staging prefix. Asserted here
# because CI runs this sequence and a packaging regression is invisible
# to the pixel battery. SOVERSION tracks PROJECT_VERSION_MAJOR, so the
# SONAME symlink is lib<name>.so.<major> over the full-version file.
# The version is read back from the installed recipe rather than written
# here: a literal in this list went stale at the 0.2.0 bump (it still
# named the 0.1.1 files), and the .pc file is the one installed artifact
# that carries PROJECT_VERSION in text form.
file(STRINGS ${STAGE}/lib/pkgconfig/imprint.pc _pc_version_line REGEX "^Version:")
string(REGEX REPLACE "^Version:[ \t]*" "" IMPRINT_VERSION "${_pc_version_line}")
if (NOT IMPRINT_VERSION)
    message(FATAL_ERROR "installed imprint.pc carries no Version: line")
endif()
string(REGEX REPLACE "^([0-9]+).*" "\\1" _major "${IMPRINT_VERSION}")
foreach(_f lib/libimcore.so.${_major} lib/libimcore.so.${IMPRINT_VERSION}
             lib/libzbapi.so.${_major} lib/libzbapi.so.${IMPRINT_VERSION}
             share/imprint/LICENSE lib/pkgconfig/imprint.pc)
    if (NOT EXISTS ${STAGE}/${_f})
        message(FATAL_ERROR "installed package is missing ${_f}")
    endif()
endforeach()

if (PKG_CONFIG_EXECUTABLE)
    execute_process(
        COMMAND ${PKG_CONFIG_EXECUTABLE} --define-prefix --silence-errors
            ${STAGE}/lib/pkgconfig/imprint.pc --cflags --libs
        OUTPUT_VARIABLE _pc_out
        RESULT_VARIABLE r)
    if (r)
        message(FATAL_ERROR "pkg-config cannot read the installed imprint.pc (${r})")
    endif()
    if (NOT _pc_out MATCHES "imprint/binding/include")
        message(FATAL_ERROR "imprint.pc does not expose the zbapi include dir: ${_pc_out}")
    endif()
else()
    # no pkg-config on this host (the gcc:13 CI container ships none):
    # the .pc file is still asserted above, and the flags are read back
    # literally so a broken template cannot pass unnoticed.
    file(READ ${STAGE}/lib/pkgconfig/imprint.pc _pc_text)
    if (NOT _pc_text MATCHES "imprint/binding/include")
        message(FATAL_ERROR "imprint.pc does not expose the zbapi include dir")
    endif()
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND}
        -S ${SRC}/test/installed_smoke
        -B ${BLD}
        -DIMPRINT_STAGING_PREFIX=${STAGE}
        -DIMPRINT_SMOKE_VERSION=${IMPRINT_VERSION}
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
