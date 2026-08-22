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
if (UNIX AND NOT NDS_PLATFORM)
    option(ZBAPI_STATIC_LIBSTDCXX "Embed libstdc++ into the host .so (breaks hosts that load another libstdc++, e.g. python+pygame)" OFF)
    set(ZBAPI_HOST_LINK_OPTIONS -Wl,-rpath,$ORIGIN)
    if (ZBAPI_STATIC_LIBSTDCXX)
        list(APPEND ZBAPI_HOST_LINK_OPTIONS -static-libstdc++)
    endif()
endif()

# glibc shim (__libc_single_threaded, see host_compat.c) for host-facing
# shared libraries
if (UNIX AND NOT NDS_PLATFORM)
    add_library(zbcompat STATIC src/host_compat.c)
endif()

add_library(
    zbapi SHARED
    src/zbapi.cpp
)

target_include_directories(
    zbapi
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
    zbapi
    PUBLIC
    imapp
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

# C smoke test driving the app like a host shell would (host-only)
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

add_test(NAME zbapi COMMAND test_zbapi)
endif()