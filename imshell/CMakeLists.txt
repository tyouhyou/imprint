if(WIN32)
    set(build_src main.cpp win_input.cpp)
    set(EXEFMT WIN32)
    set(APPSRCDIR ${CMAKE_CURRENT_SOURCE_DIR}/src/win)
elseif(NDS_PLATFORM)
    set(build_src main.cpp arm9_packing.cpp)
    set(EXEFMT)
    set(APPSRCDIR ${CMAKE_CURRENT_SOURCE_DIR}/src/nds)
    set(build_inc ${LIBNDS}/include ${DEVKITPRO}/calico/include ${CMAKE_CURRENT_SOURCE_DIR}/include/nds)
    set(build_lib nds9 calico_ds9)
    set(NDS_LIBDIR ${LIBNDS}/lib ${DEVKITPRO}/calico/lib)
elseif(APPLE)
    # A-20: the AppKit shell (NSWindow/NSView, CGImage presentation).
    # One codebase for every macOS the C++ standard supports: no 13/14
    # splits, the deployment target decides
    set(IM_SHELL_BACKEND "AppKit" CACHE STRING "macOS shell backend to build: AppKit")
    set_property(CACHE IM_SHELL_BACKEND PROPERTY STRINGS AppKit)
    if (NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING
            "macOS deployment target (A-20: one codebase, no 13/14 split)" FORCE)
    endif()
    enable_language(OBJCXX)
    set(build_src main.mm)
    set(EXEFMT)
    set(APPSRCDIR ${CMAKE_CURRENT_SOURCE_DIR}/src/mac)
    set(build_lib "-framework Cocoa")
    set_source_files_properties(${APPSRCDIR}/main.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
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
        set(build_inc)
        set(build_lib)
        set(APPSRCDIR ${CMAKE_CURRENT_SOURCE_DIR}/src/linux)
    else()
        message(FATAL_ERROR "Unknown IM_SHELL_BACKEND: ${IM_SHELL_BACKEND}")
    endif()
    set(EXEFMT)
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

add_executable(
    ${STORY} ${EXEFMT}
    ${APPSRC}
)

target_link_libraries(
    ${STORY}

    PRIVATE
    ${IMUTILLIB}
    ${IMCORELIB}
    ${IMEVENTLIB}
    ${IMINPUTLIB}
    ${IMAPPLIB}
    ${STORY}_app
    ${build_lib}
)

if (TARGET shell_common)
    target_link_libraries(${STORY} PRIVATE shell_common)
    target_include_directories(${STORY} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
endif()

if (NDS_LIBDIR)
    target_link_directories(${STORY} PRIVATE ${NDS_LIBDIR})
endif()

if (NDS_PLATFORM)
    # package the ARM9 executable into a .nds ROM after linking
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

target_include_directories(
    ${STORY}

    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${build_inc}
)