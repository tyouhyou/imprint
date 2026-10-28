# Installed-config smoke (Batch G P1.5 / X-1): a foreign mini project
# consuming the INSTALLED package. The staging prefix is passed by the
# driver: -DIMPRINT_STAGING_PREFIX=<cmake --install output>. CI
# (linux-desktop) runs the install + configure + build + run sequence
# directly; test/CMakeLists.txt registers the same sequence with
# ctest --build-and-test for the local path.
cmake_minimum_required(VERSION 3.16)
project(imprint_installed_smoke CXX)

if (NOT IMPRINT_STAGING_PREFIX)
    message(FATAL_ERROR "pass -DIMPRINT_STAGING_PREFIX=<install prefix>")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# the installed shared imcore lives under the staging prefix's bin/;
# the smoke executable joins it so the .so is found at runtime
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

enable_testing()

# X-14: the requested version comes from the installed recipe (its
# `Version:` line is generated from PROJECT_VERSION), so a minor bump
# leaves no stale literal behind here -- and asking for exactly the
# installed version proves the generated ConfigVersion file accepts its
# own tree. That check is not cosmetic: the compatibility mode is
# SameMinorVersion, so a 0.1 request does not resolve against 0.2.
if (NOT IMPRINT_SMOKE_VERSION)
    file(STRINGS ${IMPRINT_STAGING_PREFIX}/lib/pkgconfig/imprint.pc _pc_version_line REGEX "^Version:")
    string(REGEX REPLACE "^Version:[ \t]*" "" IMPRINT_SMOKE_VERSION "${_pc_version_line}")
endif()
if (NOT IMPRINT_SMOKE_VERSION)
    message(FATAL_ERROR "cannot read the installed imprint.pc version")
endif()

find_package(imprint ${IMPRINT_SMOKE_VERSION} CONFIG REQUIRED
    PATHS ${IMPRINT_STAGING_PREFIX}
    NO_DEFAULT_PATH)

# the installed shared imcore stays in the staging prefix's lib/; the
# build rpath pins the smoke executable to it (test isolation, no env)
set(CMAKE_BUILD_RPATH ${IMPRINT_STAGING_PREFIX}/lib)

add_executable(installed_smoke main.cpp)
# X-1: the C ABI travels in the export set with its headers; a C++ consumer
# of imprint::zbapi proves the include path, the link, and that the story
# app stays baked inside the shared library instead of leaking into the
# consumer's link interface.
target_link_libraries(installed_smoke PRIVATE imprint::imapp_canvas imprint::snapshot imprint::zbapi)

# X-2: linking the shell target resolves its platform requirements through
# the export — X11 arrives as an imported target the package config finds
# (the audit's `cannot find -lX11` failure, which this smoke could not see
# before). Gated on shell_hostloop, not shell_backend: the library-mode
# loop below exists only where the backend has one (SIXEL keeps a
# framework-mode main, code-contract §11.3). Skipped on macOS, where the
# AppKit loop needs the ObjC runtime a CXX-only consumer project does not
# set up.
if (TARGET imprint::shell_hostloop AND UNIX AND NOT APPLE)
    target_link_libraries(installed_smoke PRIVATE imprint::shell_backend)
    target_compile_definitions(installed_smoke PRIVATE IM_SMOKE_SHELL=1)
endif()

add_test(NAME installed_smoke COMMAND installed_smoke)
