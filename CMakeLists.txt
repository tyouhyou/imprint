cmake_minimum_required(VERSION 3.16)

# X-14: the project version is what find_package(imprint <ver>) checks
# against, so it must not trail the released tags (v0.1 / v0.1.1 are
# already out; a 0.1.0 here made `find_package(imprint 0.1.1)` fail
# against a package built from this tree). Keep it >= the newest release.
project(imprint VERSION 0.1.1)

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
# X-3: the shell is packaging, not part of the library surface. A consumer
# that wants the headless widget path (imcore / imui / imapp) must not
# inherit the platform's packages -- before this gate, add_subdirectory
# always configured imshell and the UNIX default backend made libx11-dev a
# hard requirement of every Linux consumer. ON top level (the in-tree
# build, unchanged), OFF as a subproject.
option(IMPRINT_WITH_SHELL "Build the platform shell (A-2 seams + the selected backend's usage requirements)" ${IMPRINT_IS_TOP_LEVEL})
if (IMPRINT_WITH_TESTS OR IMPRINT_WITH_DEMOS)
    set(IMPRINT_USE_APPS ON)
    set(IMPRINT_USE_TOOLS ON)
else()
    set(IMPRINT_USE_APPS OFF)
    set(IMPRINT_USE_TOOLS ${IMPRINT_WITH_TOOLS})
endif()
# the in-tree tenants of the shell say so explicitly instead of silently
# dragging it in: the battery links the A-2 seams (shell_common) and the
# ${STORY} executable is composed from shell_backend
if ((IMPRINT_WITH_TESTS OR IMPRINT_WITH_DEMOS) AND NOT IMPRINT_WITH_SHELL)
    message(FATAL_ERROR
        "IMPRINT_WITH_SHELL=OFF conflicts with IMPRINT_WITH_TESTS/IMPRINT_WITH_DEMOS "
        "(the test battery and the demo executable both need the shell); "
        "add -DIMPRINT_WITH_SHELL=ON")
endif()

# imshell's AppKit backend hands main.mm to the ${STORY} executable
# assembled in THIS directory (A-22 wiring), so OBJCXX must be enabled
# here: enable_language() covers the current directory and below only —
# an enable inside imshell's subdirectory leaves this scope without
# CMAKE_OBJCXX_COMPILE_OBJECT and the generate step fails
if (APPLE)
    enable_language(OBJCXX)
    # keep the Debug logging definition language-complete: -DDEBUG rides
    # CMAKE_CXX_FLAGS_DEBUG above, which never reaches .mm TUs — the first
    # LD in an AppKit TU would silently print nothing
    set(CMAKE_OBJCXX_FLAGS_DEBUG "${CMAKE_OBJCXX_FLAGS_DEBUG} -DDEBUG")
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

# X-8 warning baseline: the per-compiler warning set + its -Werror hook
# (cmake/imprint-warnings.cmake holds the flags and the rationale). The
# options are declared here so every target in the tree can read them.
option(IMPRINT_WARNINGS "Enable the project's per-compiler warning set" ON)
option(IMPRINT_WERROR "Treat the project's warnings as errors (default: in-tree builds only)" ${IMPRINT_IS_TOP_LEVEL})
include(cmake/imprint-warnings.cmake)
# one option per flag, so each carries its own language gate: a flag that
# exists only in C++ must never reach a C TU (gcc warns "valid for C++
# but not for C" -- and -Werror turns that into a build failure)
foreach(_imprint_flag IN LISTS IMPRINT_WARNING_FLAGS_C)
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:${_imprint_flag}>")
endforeach()
foreach(_imprint_flag IN LISTS IMPRINT_WARNING_FLAGS_CXX)
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:CXX>:${_imprint_flag}>"
        "$<$<COMPILE_LANGUAGE:OBJCXX>:${_imprint_flag}>")
endforeach()
# the error gate is one flag for every compiled language (adding it per
# language would emit it twice on a C++ TU)
if (IMPRINT_WERROR_FLAG)
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX,OBJCXX>:${IMPRINT_WERROR_FLAG}>")
endif()
# definitions the per-compiler set needs (MSVC's CRT opt-outs); the
# module owns them so a reader finds the whole set in one file
if (IMPRINT_WARNING_DEFINITIONS)
    add_compile_definitions(${IMPRINT_WARNING_DEFINITIONS})
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# CTest root: enable_testing() must run in the top-level file for
# add_test() in subdirectories to register (C8). A subproject never
# registers imprint tests (the IMPRINT_WITH_* gates default OFF), so
# the consumer's own testing setup stays untouched.
if (IMPRINT_IS_TOP_LEVEL)
    enable_testing()
endif()

# Pixel model resolution, hoisted to the top level (A-29 Phase 1: imraster
# and other module CMakeLists need the resolved model before imcore's
# subdirectory runs; the decision itself is unchanged from the block that
# used to live in imcore/CMakeLists.txt). COLOR_DEPTH=16 is meant for
# embedded displays (NDS): the desktop shells assume 32bpp (e.g. the Win32
# DIB blit), so 16bpp builds are only supported on embedded targets.
set(COLOR_DEPTH 32 CACHE STRING "Pixel color depth in bits (16 or 32). Set 16 for embedded framebuffers only.")
# 32bpp override pair: both must be set for either to take effect (a lone
# one is a configuration error). They select the pixel traits combo
# directly, e.g. -DRGB_MODEL=rgba32 -DENDIAN=le for the imraster config.
set(RGB_MODEL "" CACHE STRING "32bpp RGB model override (must pair with ENDIAN; e.g. rgba32/bgra32/argb32).")
set(ENDIAN "" CACHE STRING "32bpp endianness override (must pair with RGB_MODEL; le/be).")
if ((RGB_MODEL AND NOT ENDIAN) OR (ENDIAN AND NOT RGB_MODEL))
    message(FATAL_ERROR "RGB_MODEL and ENDIAN are a pair: set both or neither "
                        "(got RGB_MODEL=\"${RGB_MODEL}\" ENDIAN=\"${ENDIAN}\")")
endif()
if (COLOR_DEPTH EQUAL 16)
    # 16bpp: abgr1555, the only 16bpp model provided by pixel_traits.hpp
    # (overrides are ignored -- there is nothing else to select)
    set(IMPRINT_RGB_MODEL abgr)
    set(IMPRINT_ENDIAN 1555)
else()
    if (RGB_MODEL)
        set(IMPRINT_RGB_MODEL ${RGB_MODEL})
        set(IMPRINT_ENDIAN ${ENDIAN})
    else()
        # 32bpp BGRA on little-endian x86: MSVC/APPLE use bgra32+le, UNIX uses
        # argb32+be — both expand to the same byte order {b,g,r,a} (A-11). The
        # traits alias names are endian-relative; see ARCHITECTURE.md §4.4.
        if (MSVC OR WIN32)
            set(IMPRINT_RGB_MODEL bgra32)
            set(IMPRINT_ENDIAN le)
        elseif (APPLE)
            set(IMPRINT_RGB_MODEL bgra32)
            set(IMPRINT_ENDIAN le)
        elseif (UNIX)
            set(IMPRINT_RGB_MODEL argb32)
            set(IMPRINT_ENDIAN be)
        endif ()
    endif()
endif()
message(STATUS "imprint pixel model: COLOR_DEPTH=${COLOR_DEPTH} RGB_MODEL=${IMPRINT_RGB_MODEL} ENDIAN=${IMPRINT_ENDIAN}")

add_subdirectory(imutil)
add_subdirectory(imcore)
# the optional 3D raster module (A-29 / ARCHITECTURE §4.12): default OFF,
# 32bpp rgba32/le only, and not in the install/export set yet (A-28's
# feature inventory records the packaging decision)
add_subdirectory(imraster)
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
if (IMPRINT_WITH_SHELL)
    add_subdirectory(imshell)
endif()

# imprint:: aliases: the out-of-tree consumption names (ARCHITECTURE
# §3.2). Aliases resolve inside the consuming tree.
foreach(_imprint_target imutil imcore imraster imevent iminput imui imapp imapp_canvas imapp_snapshot shell_common shell_hostloop shell_backend zbapi)
    if (TARGET ${_imprint_target})
        add_library(imprint::${_imprint_target} ALIAS ${_imprint_target})
    endif()
endforeach()
# the snapshot helper's consumption name (code-contract §12)
if (TARGET imapp_snapshot)
    add_library(imprint::snapshot ALIAS imapp_snapshot)
endif()

# Batch G P1.5: install/export. `cmake --install` ships the library-mode
# surface, and find_package(imprint CONFIG) afterwards provides the same
# imprint:: targets the add_subdirectory path does (ARCHITECTURE §3.2).
# Header layout: each module's -I root is reproduced verbatim under
# include/imprint/<module>/include, matching the INSTALL_INTERFACE
# generator expressions in the module files. The rules are inert on
# targets that never install (NDS toolchain, framework-mode builds that
# never run cmake --install). Subproject mode never installs: see the
# gate around the install rules below -- the add_subdirectory path
# consumes the targets directly, no install round-trip.
set(IMPRINT_INSTALL_TARGETS "")
foreach(_imprint_target imutil imcore imevent iminput imui imapp imapp_canvas imapp_snapshot shell_common shell_hostloop shell_backend)
    if (TARGET ${_imprint_target})
        list(APPEND IMPRINT_INSTALL_TARGETS ${_imprint_target})
    endif()
endforeach()
# X-1: the C ABI is the foreign-host surface (ARCHITECTURE §4.8) -- the
# Python / WASM / C hosts and every cross-language consumer ride it, so the
# package installs it whenever the binding was built (framework mode).
# Not on the NDS: the toolchain has no shared libraries, so zbapi degrades
# to STATIC there and its PRIVATE story-app dependency becomes an export
# requirement for a target no package installs (nothing loads a C ABI on
# the ARM9; checked by the nds-build verify target).
if (TARGET zbapi AND NOT NDS_PLATFORM)
    list(APPEND IMPRINT_INSTALL_TARGETS zbapi)
endif()
# the glibc shim travels with the shared imcore as a LINK_ONLY
# dependency; the export set must contain it (binding/CMakeLists)
if (TARGET zbcompat)
    list(APPEND IMPRINT_INSTALL_TARGETS zbcompat)
endif()
# the exported consumption name (the alias imprint::snapshot has no
# install form; the property renames the target inside the export set)
if (TARGET imapp_snapshot)
    set_target_properties(imapp_snapshot PROPERTIES EXPORT_NAME snapshot)
endif()
# Subproject mode never installs: a consumer's `cmake --install` must
# not ship ~7 imprint libraries into ITS prefix uninvited (ARCHITECTURE
# §3.2: subprojects configure libraries only) -- the add_subdirectory
# path consumes the targets directly, no install round-trip.
if (IMPRINT_IS_TOP_LEVEL)
install(TARGETS ${IMPRINT_INSTALL_TARGETS} EXPORT imprintTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)
install(EXPORT imprintTargets
    FILE imprintTargets.cmake
    NAMESPACE imprint::
    DESTINATION lib/cmake/imprint)
set(IMPRINT_INSTALL_INCLUDE_MODULES imutil imcore imevent iminput imui imapp)
# X-3: the shell's headers ship only with the shell
if (TARGET shell_backend)
    list(APPEND IMPRINT_INSTALL_INCLUDE_MODULES imshell)
endif()
# X-1: zbapi.h ships beside the binding, and only when the binding is
# installed -- a header without its library is a trap, not a package
# (NDS excluded for the same reason as the target list above)
if (TARGET zbapi AND NOT NDS_PLATFORM)
    list(APPEND IMPRINT_INSTALL_INCLUDE_MODULES binding)
endif()
foreach(_imprint_module IN LISTS IMPRINT_INSTALL_INCLUDE_MODULES)
    install(DIRECTORY ${_imprint_module}/include/
            DESTINATION include/imprint/${_imprint_module}/include)
endforeach()
# X-14: an installed package without its license is unshippable for most
# downstream policies (and CMake's own install rules carry no default).
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
        DESTINATION share/imprint)
include(CMakePackageConfigHelpers)
configure_package_config_file(
    cmake/imprintConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/imprintConfig.cmake
    INSTALL_DESTINATION lib/cmake/imprint)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/imprintConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/imprintConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/imprintConfigVersion.cmake
    DESTINATION lib/cmake/imprint)
# X-14: a pkg-config recipe for the C ABI. The CMake package is the
# primary consumption path (it carries the C++ targets); the .pc file
# serves the non-CMake consumer that links libzbapi directly. Only
# shipped when the binding was built — the same gate as zbapi.h above.
if (TARGET zbapi AND NOT NDS_PLATFORM)
    configure_file(cmake/imprint.pc.in
                   ${CMAKE_CURRENT_BINARY_DIR}/imprint.pc
                   @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/imprint.pc
            DESTINATION lib/pkgconfig)
endif()
endif()

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
    # links the full imui (the html_embed precedent): pass 1 gates on
    # materializes_widget, which needs the widget factory, not just the
    # parser — a document of only unknown tags must fail the build
    add_executable(ui_embed tools/ui_embed.cpp)
    target_link_libraries(ui_embed PRIVATE imui)
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
    # generic binary embedder (the html_embed convention): today the
    # showcase's packed Inter TTF blob (runtime TTF text in the demo)
    add_executable(bytes_embed tools/bytes_embed.cpp)
    # design-file renderer CLI (Batch G P2): .ui/.html in, PNG/GIF out,
    # the frame hash on stdout. PNG rides its own vendored stb writer TU
    # so the default zero-option build renders PNG (USE_PNG stays a
    # library-codec option)
    add_executable(imprint-render tools/imprint_render.cpp)
    target_link_libraries(imprint-render PRIVATE imui imapp_snapshot)
    # SYSTEM: vendored stb stays out of the project warning set
    target_include_directories(imprint-render SYSTEM PRIVATE
        ${PROJECT_SOURCE_DIR}/third_party/stb)
endif()

if (IMPRINT_WITH_TESTS)
add_subdirectory(test)
endif()
