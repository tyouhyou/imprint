cmake_minimum_required(VERSION 3.16)

project(myapp)

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

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")

# MSVC: source and execution charset UTF-8. Hosts with a non-UTF-8
# code page (e.g. 932) otherwise mangle the non-ASCII literals in
# test_text (C4819) and the tests fail only on that machine
if (MSVC)
    add_compile_options(/utf-8)
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory(imutil)
add_subdirectory(imcore)
add_subdirectory(imevent)
add_subdirectory(iminput)
add_subdirectory(imui)
add_subdirectory(imapp)
add_subdirectory(apps)
add_subdirectory(binding)
add_subdirectory(imshell)

# A-22: the one composition point. imshell provides shell_backend (the
# selected backend's usage requirements) + shell_common (A-2 seams) and
# exports its backend main sources via IMPRINT_SHELL_*; apps provide
# <story>_app. The executable is assembled here so framework modules
# never name story code.
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

# build-time UI design file packer (validates with the library parser,
# embeds documents as byte arrays). Host tool only: embedded targets
# ship pre-packed documents from their own build.
if (NOT NDS_PLATFORM)
    add_executable(ui_embed tools/ui_embed.cpp imui/src/ui_file.cpp)
    target_include_directories(ui_embed PRIVATE
        ${CMAKE_SOURCE_DIR}/imui/include
        ${CMAKE_SOURCE_DIR}/imutil/include
    )
    # procedural showcase asset generator (V-2): emits RGBA8 arrays at
    # build time — the ttf_subset precedent (build-time materialization
    # of a shipping asset), no runtime decode and no USE_PNG dependency
    add_executable(asset_gen tools/asset_gen.cpp)
endif()

add_subdirectory(test)
