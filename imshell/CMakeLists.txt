# A-22: imshell is a pure provider. It builds `shell_common` (the A-2
# seams, shared with the test battery) and exports `shell_backend` — the
# selected platform's usage requirements — plus the backend main sources
# through the IMPRINT_SHELL_* variables. The ${STORY} executable is
# composed at the top level (CMakeLists.txt): backend + <story>_app.
# imshell never names story code.

if(WIN32)
    set(build_src main.cpp win_input.cpp)
    set(APPSRCDIR ${CMAKE_CURRENT_SOURCE_DIR}/src/win)
    set(IMPRINT_EXE_WIN32 ON PARENT_SCOPE)
elseif(NDS_PLATFORM)
    set(build_src main.cpp arm9_packing.cpp)
    set(APPSRCDIR ${CMAKE_CURRENT_SOURCE_DIR}/src/nds)
    set(build_inc ${LIBNDS}/include ${DEVKITPRO}/calico/include ${CMAKE_CURRENT_SOURCE_DIR}/include/nds)
    set(build_lib nds9 calico_ds9)
    set(NDS_LIBDIR ${LIBNDS}/lib ${DEVKITPRO}/calico/lib)
elseif(APPLE)
    # A-20: the AppKit shell (NSWindow/NSView, CGImage presentation).
    # One codebase for every macOS the C++ standard supports: no 13/14
    # splits, the deployment target decides. OBJCXX itself is enabled at
    # the top level: the ${STORY} exe consumes main.mm there, and
    # enable_language() does not propagate upward from this subdirectory
    set(IM_SHELL_BACKEND "AppKit" CACHE STRING "macOS shell backend to build: AppKit")
    set_property(CACHE IM_SHELL_BACKEND PROPERTY STRINGS AppKit)
    if (NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING
            "macOS deployment target (A-20: one codebase, no 13/14 split)" FORCE)
    endif()
    set(build_src main.mm)
    set(APPSRCDIR ${CMAKE_CURRENT_SOURCE_DIR}/src/mac)
    set(build_lib "-framework Cocoa")
    set(IMPRINT_SHELL_OBJC_ARC ON PARENT_SCOPE)
elseif(UNIX)
    set(IM_SHELL_BACKEND "X11" CACHE STRING "Linux shell backend to build: X11 or FB")
    set_property(CACHE IM_SHELL_BACKEND PROPERTY STRINGS X11 FB)

    if(${IM_SHELL_BACKEND} STREQUAL "X11")
        set(build_src main_x.cpp x11_input.cpp)
        set(build_inc /usr/include)
        set(build_lib X11)
        set(APPSRCDIR ${CMAKE_CURRENT_SOURCE_DIR}/src/linux)
    elseif(${IM_SHELL_BACKEND} STREQUAL "FB")
        set(build_src fb.cpp main_fb.cpp)
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

# A-2 shell seams (input_source.cpp feeds the app; presenter.hpp is
# header-only): one static lib shared by every desktop shell and the
# test battery. Embedded targets keep their minimal build.
if (NOT NDS_PLATFORM)
    add_library(shell_common STATIC src/shell/input_source.cpp)
    target_include_directories(shell_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(shell_common PUBLIC ${IMAPPLIB} ${IMINPUTLIB})
endif()

# the selected backend's usage requirements; the top level links ${STORY}
# against it and compiles the backend main sources itself (CMake rejects
# a zero-source executable, so the mains stay exe sources)
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

# exported to the top level for the ${STORY} executable
set(IMPRINT_SHELL_SRC ${APPSRC} PARENT_SCOPE)
set(IMPRINT_SHELL_SRCDIR ${APPSRCDIR} PARENT_SCOPE)
