if (MSVC OR WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# Linux host build: the .so is loaded by external hosts (python, etc.), so
#   - resolve sibling shared libs (libimcore.so) next to it via $ORIGIN
#   - libstdc++ links dynamically by default: a statically embedded copy
#     breaks any host process that already loads a dynamic libstdc++
#     (python+pygame segfaults in locale/codecvt state) -- two libstdc++
#     copies in one process are unsupported. ZBAPI_STATIC_LIBSTDCXX=ON
#     embeds it anyway for the "copy the .so to an older host" case.
if (UNIX AND NOT APPLE AND NOT NDS_PLATFORM)
    option(ZBAPI_STATIC_LIBSTDCXX "Embed libstdc++ into the host .so (breaks hosts that load another libstdc++, e.g. python+pygame)" OFF)
    set(ZBAPI_HOST_LINK_OPTIONS -Wl,-rpath,$ORIGIN)
    if (ZBAPI_STATIC_LIBSTDCXX)
        list(APPEND ZBAPI_HOST_LINK_OPTIONS -static-libstdc++)
    endif()
endif()

# glibc shim (__libc_single_threaded, see host_compat.c) for host-facing
# shared libraries -- ELF/glibc only; BSD/macOS libc has its own symbols
# and clang never emits the gcc fast-path references
if (UNIX AND NOT APPLE AND NOT NDS_PLATFORM)
    add_library(zbcompat STATIC src/host_compat.c)
endif()

add_library(
    zbapi SHARED
    src/zbapi.cpp
)

# X-14: same reasoning as imcore's SOVERSION — the C ABI is loaded by
# foreign hosts (python, wasm, C), so an in-place upgrade must not
# silently swap the library under a running host.
if (NOT NDS_PLATFORM)
    set_target_properties(zbapi PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR})
endif()

# X-1: zbapi is in the P1.5 install/export set, so its include directories
# must carry INSTALL_INTERFACE (CMake rejects a raw source-dir path in
# INTERFACE_INCLUDE_DIRECTORIES at generate time -- the same failure that
# broke the NDS configure after 4bc1b72). The installed layout mirrors the
# module convention: include/imprint/<module>/include.
target_include_directories(
    zbapi
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/imprint/binding/include>
)

# X-1: the C++ libraries behind the C ABI are implementation detail. PRIVATE
# is what makes the export set valid: ${STORY}_app is an app library and is
# deliberately never installed, and a SHARED library already bakes every
# static dependency in — a consumer of imprint::zbapi links the C ABI alone.
target_link_libraries(
    zbapi
    PRIVATE
    imapp
    imapp_canvas
    imui
    ${STORY}_app
)

if (ZBAPI_HOST_LINK_OPTIONS)
    target_link_options(zbapi PRIVATE ${ZBAPI_HOST_LINK_OPTIONS})
endif()

if (TARGET zbcompat)
    # both host-facing shared libs need the glibc shim: libimcore.so is a
    # separate shared lib loaded alongside zbapi. Same for the embedded
    # libstdc++: imcore is built by the same toolchain as zbapi.
    target_link_libraries(zbapi PRIVATE zbcompat)
    target_link_libraries(${IMCORELIB} zbcompat)
    target_link_options(${IMCORELIB} PRIVATE ${ZBAPI_HOST_LINK_OPTIONS})
endif()

# C smoke test driving the app like a host shell would (host-only).
# Its assertions ARE the test and its source #errors under NDEBUG, so the
# target must never see NDEBUG -- including at a Release build type, where
# the tree-wide flags carry -DNDEBUG. Undefining NDEBUG for this one TU
# keeps the smoke in every configuration (a plain Release build of the tree
# has to complete) and keeps it in ctest, which dropping it from `all`
# would not.
if (NOT NDS_PLATFORM)
add_executable(
    test_zbapi
    test/test_zbapi.c
)

target_link_libraries(
    test_zbapi
    PRIVATE
    zbapi
)

if (MSVC)
    target_compile_options(test_zbapi PRIVATE /UNDEBUG)
else()
    target_compile_options(test_zbapi PRIVATE -UNDEBUG)
endif()

add_test(NAME zbapi COMMAND test_zbapi)
endif()