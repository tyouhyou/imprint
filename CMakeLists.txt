cmake_minimum_required(VERSION 3.16)

project(imprint)

# Subproject detection (CMake 3.16 has no PROJECT_IS_TOP_LEVEL): when
# imprint is consumed via add_subdirectory/FetchContent, CMAKE_SOURCE_DIR
# names the consumer's project and the gates below default to libraries
# only (ARCHITECTURE §3.2). A top-level build keeps the full tree.
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(IMPRINT_IS_TOP_LEVEL ON)
else()
    set(IMPRINT_IS_TOP_LEVEL OFF)
endif()

# A-23 / Batch G P1: configure-time packaging gates. The options state
# standalone intent; the effective switches are resolved right after
# (the test battery needs the demo app libraries and the ui_embed tool,
# the demo composition implies both).
option(IMPRINT_WITH_TOOLS "Also build the host tools (ui_embed, html_embed, asset_gen)" ${IMPRINT_IS_TOP_LEVEL})
option(IMPRINT_WITH_TESTS "Build the test battery (implies the demo app libraries and the host tools)" ${IMPRINT_IS_TOP_LEVEL})
option(IMPRINT_WITH_DEMOS "Build the demo app libraries, the binding, and the STORY demo executable (framework mode)" ${IMPRINT_IS_TOP_LEVEL})
if (IMPRINT_WITH_TESTS OR IMPRINT_WITH_DEMOS)
    set(IMPRINT_USE_APPS ON)
    set(IMPRINT_USE_TOOLS ON)
else()
    set(IMPRINT_USE_APPS OFF)
    set(IMPRINT_USE_TOOLS ${IMPRINT_WITH_TOOLS})
endif()

# imshell's AppKit backend hands main.mm to the ${STORY} executable
# assembled in THIS directory (A-22 wiring), so OBJCXX must be enabled
# here: enable_language() covers the current directory and below only —
# an enable inside imshell's subdirectory leaves this scope without
# CMAKE_OBJCXX_COMPILE_OBJECT and the generate step fails
if (APPLE)
    enable_language(OBJCXX)
endif()

set(IMAPPLIB imapp)
set(IMCORELIB imcore)
set(IMUTILLIB imutil)
set(IMEVENTLIB imevent)
set(IMINPUTLIB iminput)
set(IMUILIB imui)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# host shared libs (zbapi) link against the static libs; -fPIC is required
# on linux/elf (msvc and the nds toolchain don't need it)
if (NOT MSVC AND NOT NDS_PLATFORM)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# logging verbosity (2026-09-06 ruling): debug-level logs (LD) print only
# when the build defines DEBUG. The Debug configuration gets the definition
# here; any other build that needs the traces (e.g. the NDS touch
# calibration) turns LOGGING_DEBUG on -- the safe way to add the definition
# beside a toolchain-provided CMAKE_CXX_FLAGS (a raw -DCMAKE_CXX_FLAGS=...
# would drop the NDS toolchain's INIT flags). See logging.hpp / ARCHITECTURE 4.9.
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")
set(LOGGING_DEBUG OFF CACHE BOOL "Keep the debug-level logs (LD) in this build by defining DEBUG (the Debug configuration always has them).")
if (LOGGING_DEBUG)
    add_compile_definitions(DEBUG)
endif()

# MSVC: source and execution charset UTF-8. Hosts with a non-UTF-8
# code page (e.g. 932) otherwise mangle the non-ASCII literals in
# test_text (C4819) and the tests fail only on that machine
if (MSVC)
    add_compile_options(/utf-8)
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# CTest root: enable_testing() must run in the top-level file for
# add_test() in subdirectories to register (C8)
enable_testing()

add_subdirectory(imutil)
add_subdirectory(imcore)
add_subdirectory(imevent)
add_subdirectory(iminput)
add_subdirectory(imui)
add_subdirectory(imapp)
if (IMPRINT_USE_APPS)
    add_subdirectory(apps)
endif()
if (IMPRINT_WITH_DEMOS)
    add_subdirectory(binding)
endif()
add_subdirectory(imshell)

# imprint:: aliases: the out-of-tree consumption names (ARCHITECTURE
# §3.2). Aliases resolve inside the consuming tree; install/export is
# condition-triggered later work.
foreach(_imprint_target imutil imcore imevent iminput imui imapp imapp_canvas shell_common shell_hostloop shell_backend)
    if (TARGET ${_imprint_target})
        add_library(imprint::${_imprint_target} ALIAS ${_imprint_target})
    endif()
endforeach()

# A-22: the one composition point (framework mode, IMPRINT_WITH_DEMOS).
# imshell provides shell_backend (the selected backend's usage
# requirements) + shell_common (A-2 seams) and exports its backend main
# sources via IMPRINT_SHELL_*; apps provide <story>_app. The executable
# is assembled here so framework modules never name story code.
if (IMPRINT_WITH_DEMOS)
add_executable(${STORY})
target_sources(${STORY} PRIVATE ${IMPRINT_SHELL_SRC})
target_include_directories(${STORY} PRIVATE ${IMPRINT_SHELL_SRCDIR})
if (IMPRINT_EXE_WIN32)
    set_property(TARGET ${STORY} PROPERTY WIN32_EXECUTABLE ON)
endif()
if (IMPRINT_SHELL_OBJC_ARC)
    set_source_files_properties(${IMPRINT_SHELL_SRC} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()
target_link_libraries(${STORY} PRIVATE shell_backend ${STORY}_app)

if (NDS_PLATFORM)
    # package the ARM9 executable into a .nds ROM after linking (moved
    # from imshell in A-22: the exe target lives here)
    set(NDS_ARM7 $ENV{DEVKITPRO}/calico/bin/ds7_maine.elf)
    if (NOT EXISTS ${NDS_ARM7})
        message(FATAL_ERROR "ARM7 binary not found: ${NDS_ARM7}")
    endif()
    set(NDS_ROM ${CMAKE_BINARY_DIR}/bin/${STORY}.nds)
    add_custom_command(
        TARGET ${STORY} POST_BUILD
        COMMAND $ENV{DEVKITPRO}/tools/bin/ndstool
                -c ${NDS_ROM}
                -9 $<TARGET_FILE:${STORY}>
                -7 ${NDS_ARM7}
        COMMENT "Packaging ${NDS_ROM}"
        VERBATIM
    )
endif()
endif()

# build-time UI design file packer (validates with the library parser,
# embeds documents as byte arrays). Host tool only: embedded targets
# ship pre-packed documents from their own build.
if (IMPRINT_USE_TOOLS AND NOT NDS_PLATFORM)
    add_executable(ui_embed tools/ui_embed.cpp imui/src/ui_file.cpp)
    target_include_directories(ui_embed PRIVATE
        ${PROJECT_SOURCE_DIR}/imui/include
        ${PROJECT_SOURCE_DIR}/imutil/include
    )
    # HTML design file packer (the ui_embed precedent for parse_html):
    # validates with the library parser, embeds documents as byte arrays
    add_executable(html_embed tools/html_embed.cpp)
    target_link_libraries(html_embed PRIVATE imui)
    target_include_directories(html_embed PRIVATE
        ${PROJECT_SOURCE_DIR}/imui/include
        ${PROJECT_SOURCE_DIR}/imutil/include
    )
    # procedural showcase asset generator (V-2): emits RGBA8 arrays at
    # build time — the ttf_subset precedent (build-time materialization
    # of a shipping asset), no runtime decode and no USE_PNG dependency
    add_executable(asset_gen tools/asset_gen.cpp)
endif()

if (IMPRINT_WITH_TESTS)
add_subdirectory(test)
endif()
