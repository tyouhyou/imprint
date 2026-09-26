# A-22: imshell is a pure provider. It builds `shell_common` (the A-2
# seams, shared with the test battery) and `shell_hostloop` (the
# library-mode host loops, zb::shell::run — code-contract §11), and
# exports `shell_backend` — the selected platform's usage requirements —
# plus the backend main sources through the IMPRINT_SHELL_* variables.
# The ${STORY} executable is composed at the top level (CMakeLists.txt):
# backend + <story>_app. imshell never names story code.

if(WIN32)
    # the host loop carries the whole backend implementation (run +
    # the input translator): a library consumer links shell_backend and
    # gets everything, the exe compiles only its thin main
    set(build_src main.cpp)
    set(hostloop_src src/win/run_win.cpp src/win/win_input.cpp)
    set(APPSRCDIR ${CMAKE_CURRENT_SOURCE_DIR}/src/win)
    set(IMPRINT_EXE_WIN32 ON PARENT_SCOPE)
elseif(NDS_PLATFORM)
    # no shell_hostloop on the NDS: the firmware entry stays the
    # framework-mode main (code-contract §11.3)
    set(build_src main.cpp arm9_packing.cpp)
    set(APPSRCDIR ${CMAKE_CURRENT_SOURCE_DIR}/src/nds)
    set(build_inc ${LIBNDS}/include ${DEVKITPRO}/calico/include ${CMAKE_CURRENT_SOURCE_DIR}/include/nds)
    set(build_lib nds9 calico_ds9)
    set(NDS_LIBDIR ${LIBNDS}/lib ${DEVKITPRO}/calico/lib)
elseif(APPLE)
    # A-20: the AppKit shell (NSWindow/NSView, CGImage presentation).
    # One codebase for every macOS the C++ standard supports: no 13/14
    # splits. The deployment target is deliberately NOT pinned -- each
    # known build host is covered by its SDK default, and both pins we
    # tried broke one of them: Xcode 10 (the macOS 10.13.6 verification
    # machine) rejects two-digit targets like 11.0 at the clang driver
    # ("invalid version number"), while a 10.13 pin trips libc++
    # availability gates (std::get<T>(variant) is marked "introduced in
    # macOS 10.14" -- 31 call sites in the tests alone). The shell's own
    # AppKit/CG API floor predates both SDKs, so the un-pinned SDK
    # default compiles everywhere. Pass -DCMAKE_OSX_DEPLOYMENT_TARGET=
    # <ver> explicitly only if a pinned distribution build is ever
    # needed (and mind the 10.14 availability gates). OBJCXX itself is
    # enabled at the top level: the ${STORY} exe consumes main.mm there,
    # and enable_language() does not propagate upward from this
    # subdirectory
    set(IM_SHELL_BACKEND "AppKit" CACHE STRING "macOS shell backend to build: AppKit")
    set_property(CACHE IM_SHELL_BACKEND PROPERTY STRINGS AppKit)
    set(build_src main.mm)
    set(hostloop_src src/mac/run_mac.mm)
    set(APPSRCDIR ${CMAKE_CURRENT_SOURCE_DIR}/src/mac)
    set(build_lib "-framework Cocoa")
    set(IMPRINT_SHELL_OBJC_ARC ON PARENT_SCOPE)
elseif(UNIX)
    set(IM_SHELL_BACKEND "X11" CACHE STRING "Linux shell backend to build: X11, FB or SIXEL")
    set_property(CACHE IM_SHELL_BACKEND PROPERTY STRINGS X11 FB SIXEL)

    if(${IM_SHELL_BACKEND} STREQUAL "X11")
        set(build_src main_x.cpp)
        set(hostloop_src src/linux/run_x11.cpp src/linux/x11_input.cpp)
        set(build_inc /usr/include)
        set(build_lib X11)
        set(APPSRCDIR ${CMAKE_CURRENT_SOURCE_DIR}/src/linux)
    elseif(${IM_SHELL_BACKEND} STREQUAL "FB")
        set(build_src main_fb.cpp)
        set(hostloop_src src/linux/run_fb.cpp src/linux/fb.cpp)
        set(APPSRCDIR ${CMAKE_CURRENT_SOURCE_DIR}/src/linux)
    elseif(${IM_SHELL_BACKEND} STREQUAL "SIXEL")
        # P4 demo target: the presenter (sixel) and the input translator
        # (term_input) are pure TUs unit-tested from the battery; this
        # main is fd glue. Framework-mode only — no run() (§11.3).
        set(build_src main_term.cpp)
        set(APPSRCDIR ${CMAKE_CURRENT_SOURCE_DIR}/src/linux)
    else()
        message(FATAL_ERROR "Unknown IM_SHELL_BACKEND: ${IM_SHELL_BACKEND}")
    endif()
else()
    message(FATAL_ERROR "Unsupported platform for imshell")
endif()

# expand each source with its directory (build_src may hold several files)
set(APPSRC)
foreach(f IN LISTS build_src)
    list(APPEND APPSRC ${APPSRCDIR}/${f})
endforeach()

# A-2 shell seams (input_source.cpp feeds the app; presenter.hpp /
# presentation.hpp are header-only, presentation.cpp carries the shared
# nearest-neighbor resample; sixel.cpp / term_input.cpp are the P4
# terminal target's pure presenter + InputSource, portable and
# unit-tested from the battery): one static lib shared by every desktop
# shell and the test battery. Embedded targets keep their minimal build.
if (NOT NDS_PLATFORM)
    add_library(shell_common STATIC
        src/shell/input_source.cpp
        src/shell/presentation.cpp
        src/linux/sixel.cpp
        src/linux/term_input.cpp
    )
    target_include_directories(shell_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(shell_common PUBLIC ${IMAPPLIB} ${IMINPUTLIB})
endif()

# the selected backend's usage requirements; the top level links ${STORY}
# against it and compiles the backend main sources itself (CMake rejects
# a zero-source executable, so the mains stay exe sources). Library
# consumers link the same INTERFACE target and get shell_hostloop's
# zb::shell::run (code-contract §11) instead of a backend main.
add_library(shell_backend INTERFACE)
target_include_directories(shell_backend INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
if (build_inc)
    target_include_directories(shell_backend INTERFACE ${build_inc})
endif()
target_link_libraries(shell_backend INTERFACE ${IMAPPLIB} ${IMINPUTLIB})
if (build_lib)
    target_link_libraries(shell_backend INTERFACE ${build_lib})
endif()
if (TARGET shell_common)
    target_link_libraries(shell_backend INTERFACE shell_common)
endif()
if (NDS_LIBDIR)
    target_link_directories(shell_backend INTERFACE ${NDS_LIBDIR})
endif()

# library-mode host loop (code-contract §11): the extracted platform
# main bodies, callable as zb::shell::run(app, options) from a
# user-owned main. Non-NDS and only where the backend has one (the
# SIXEL demo target keeps its framework-mode main, §11.3).
if (NOT NDS_PLATFORM AND hostloop_src)
    add_library(shell_hostloop STATIC ${hostloop_src})
    target_include_directories(shell_hostloop PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(shell_hostloop PUBLIC
        ${IMAPPLIB} ${IMINPUTLIB} shell_common)
    if (build_inc)
        target_include_directories(shell_hostloop PUBLIC ${build_inc})
    endif()
    if (build_lib)
        target_link_libraries(shell_hostloop PUBLIC ${build_lib})
    endif()
    if (APPLE)
        # run_mac.mm is ObjC++/ARC like the backend main it was
        # extracted from (the ${STORY} exe gets the same flag through
        # IMPRINT_SHELL_OBJC_ARC)
        target_compile_options(shell_hostloop PRIVATE -fobjc-arc)
    endif()
    target_link_libraries(shell_backend INTERFACE shell_hostloop)
endif()

# exported to the top level for the ${STORY} executable
set(IMPRINT_SHELL_SRC ${APPSRC} PARENT_SCOPE)
set(IMPRINT_SHELL_SRCDIR ${APPSRCDIR} PARENT_SCOPE)
