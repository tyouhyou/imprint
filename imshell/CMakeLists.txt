if(WIN32)
    set(build_src main.cpp)
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
    # TODO: mac shell (src/mac/main.cpp) is not implemented yet.
    message(FATAL_ERROR "The Mac shell is not implemented yet. Create imshell/src/mac/main.cpp first.")
elseif(UNIX)
    set(IM_SHELL_BACKEND "X11" CACHE STRING "Linux shell backend to build: X11 or FB")
    set_property(CACHE IM_SHELL_BACKEND PROPERTY STRINGS X11 FB)

    if(${IM_SHELL_BACKEND} STREQUAL "X11")
        set(build_src main_x.cpp)
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