if (MSVC OR WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# 16bpp is meant for embedded displays (NDS). The desktop shells assume
# 32bpp (e.g. the Win32 DIB blit), so COLOR_DEPTH=16 builds are only
# supported on embedded targets.
set(COLOR_DEPTH 32 CACHE STRING "Pixel color depth in bits (16 or 32). Set 16 for embedded framebuffers only.")
# NOTE: these options are OFF in production builds. The third-party library
# paths below are hardcoded per platform: verified working on Linux with the
# distro dev packages (libfreetype-dev/libjpeg-dev/libpng-dev); Windows needs
# the prebuilt DLLs under ext_lib; before enabling them elsewhere, prefer
# find_package()/CACHE variables over the hardcoded IMPORTED_LOCATION entries.
set(USE_FONT OFF CACHE BOOL "Switch it to ON if font processing is needed.")
set(USE_PNG OFF CACHE BOOL "Switch it to ON if png file processing is needed.")
set(USE_JPEG OFF CACHE BOOL "Switch it to ON if jpeg file processing is needed.")
# Integer-only geometry: no std::sqrt in the circle/ellipse scanlines for
# FPU-less embedded targets (the NDS toolchain sets this automatically).
# The default keeps the floating-point bounds, which are pixel-identical
# (the integer branch is the algebraic equivalent).
set(USE_INTEGER_GEOMETRY OFF CACHE BOOL "Switch it to ON for FPU-less embedded targets (integer-only circle/ellipse bounds).")
# Non-atomic shared pointer: zb::SharedPtr falls back to a plain-int
# refcount implementation for targets without atomic instructions
# (devkitARM's NDS ships no libatomic; the toolchain sets this
# automatically). The default is std::shared_ptr everywhere else. The UI
# runs on a single thread, so the non-atomic branch is safe.
set(USE_NON_ATOMIC_PTR OFF CACHE BOOL "Switch it to ON for targets without atomic instructions (non-atomic refcounts).")

if (USE_FONT)
    set(FONT_SRC
        src/text/font.cpp
    )
else()
    set(FONT_SRC)
endif()
if (USE_JPEG)
    set(JPEG_SRC
        src/codec/jpeg.cpp
    )
else()
    set(JPEG_SRC)
endif()
if (USE_PNG)
    set(PNG_SRC
        src/codec/png.cpp
    )
else()
    set(PNG_SRC)
endif()

add_library(
    ${IMCORELIB} SHARED
    src/core/graphics.cpp
    src/text/text_image.cpp
    src/text/bitmap_provider.cpp
    src/text/utf8.cpp
    ${FONT_SRC}
    ${JPEG_SRC}
    ${PNG_SRC}
)

if (USE_FONT)
    # hardcoded per-platform library paths (see the note at USE_FONT)
add_library(freetype2 SHARED IMPORTED GLOBAL)
endif()
if (USE_JPEG)
add_library(libjpeg SHARED IMPORTED GLOBAL)
endif()
if (USE_PNG)
add_library(libpng SHARED IMPORTED GLOBAL)
endif()

if(APPLE)
if (USE_FONT)
    set_target_properties(
        freetype2 PROPERTIES 
        IMPORTED_LOCATION /usr/X11/lib/libfreetype.dylib
    )
endif()
if (USE_JPEG)
    set_target_properties(
        libjpeg PROPERTIES 
        IMPORTED_LOCATION /opt/local/libjpeg/lib/libjpeg.dylib
    )
endif()
if (USE_PNG)
    set_target_properties(
        libpng PROPERTIES
        IMPORTED_LOCATION /usr/X11/lib/libpng12.0.dylib
    )
endif()
elseif(UNIX)
if (USE_FONT)
    set_target_properties(
        freetype2 PROPERTIES 
        IMPORTED_LOCATION /usr/lib/x86_64-linux-gnu/libfreetype.so
    )
endif()
if (USE_JPEG)
    set_target_properties(
        libjpeg PROPERTIES 
        IMPORTED_LOCATION /usr/lib/x86_64-linux-gnu/libjpeg.so
    )
endif()
if (USE_PNG)
    set_target_properties(
        libpng PROPERTIES 
        IMPORTED_LOCATION /usr/lib/x86_64-linux-gnu/libpng.so
    )
endif()
elseif(MSVC OR WIN32)
if (USE_FONT)
    set_target_properties(
        freetype2 PROPERTIES 
        IMPORTED_IMPLIB   ${CMAKE_CURRENT_SOURCE_DIR}\\ext_lib\\freetype\\release_dll\\win64\\freetype.lib
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}\\ext_lib\\freetype\\release_dll\\win64\\freetype.dll
    )
endif()
if (USE_JPEG)
    set_target_properties(
        libjpeg PROPERTIES
        IMPORTED_IMPLIB_RELEASE   ${CMAKE_CURRENT_SOURCE_DIR}\\ext_lib\\libjpeg\\x64\\release\\jpeg.lib
        IMPORTED_LOCATION_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}\\ext_lib\\libjpeg\\x64\\release\\jpeg.dll
        IMPORTED_IMPLIB_DEBUG     ${CMAKE_CURRENT_SOURCE_DIR}\\ext_lib\\libjpeg\\x64\\debug\\jpeg.lib
        IMPORTED_LOCATION_DEBUG   ${CMAKE_CURRENT_SOURCE_DIR}\\ext_lib\\libjpeg\\x64\\debug\\jpeg.dll
    )
endif()
if (USE_PNG)
    set_target_properties(
        libpng PROPERTIES
        IMPORTED_IMPLIB_RELEASE   ${CMAKE_CURRENT_SOURCE_DIR}\\ext_lib\\libpng\\x64\\release\\libpng16.lib
        IMPORTED_LOCATION_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}\\ext_lib\\libpng\\x64\\release\\libpng16.dll
        IMPORTED_IMPLIB_DEBUG     ${CMAKE_CURRENT_SOURCE_DIR}\\ext_lib\\libpng\\x64\\debug\\libpng16d.lib
        IMPORTED_LOCATION_DEBUG   ${CMAKE_CURRENT_SOURCE_DIR}\\ext_lib\\libpng\\x64\\debug\\libpng16d.dll
    )
endif()
endif()

if(APPLE)
set (INC_DIRS
    /Developer/SDKs/MacOSX10.6.sdk/usr/X11/include
    /Developer/SDKs/MacOSX10.6.sdk/usr/X11/include/freetype2
    /Developer/SDKs/MacOSX10.6.sdk/usr/X11/include/freetype2/freetype
    /opt/local/libjpeg/include)
elseif(UNIX)
set (INC_DIRS
    /usr/include
    /usr/include/freetype2
    /usr/include/freetype2/freetype)
elseif(MSVC OR WIN32)
set (INC_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}\\ext_lib\\freetype\\include
    ${CMAKE_CURRENT_SOURCE_DIR}\\ext_lib\\freetype\\include\\freetype
    ${CMAKE_CURRENT_SOURCE_DIR}\\ext_lib\\libjpeg\\include
    ${CMAKE_CURRENT_SOURCE_DIR}\\ext_lib\\libpng\\include)
endif()

target_include_directories(
    ${IMCORELIB} 
    
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${INC_DIRS}
    
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core
    ${CMAKE_CURRENT_SOURCE_DIR}/include/text
    ${CMAKE_CURRENT_SOURCE_DIR}/include/codec
)

if (USE_FONT)
    target_compile_definitions(${IMCORELIB} PUBLIC USE_FONT)
    target_link_libraries(${IMCORELIB} freetype2)
endif()

if (USE_INTEGER_GEOMETRY)
    target_compile_definitions(${IMCORELIB} PUBLIC USE_INTEGER_GEOMETRY)
endif()

if (USE_NON_ATOMIC_PTR)
    target_compile_definitions(${IMCORELIB} PUBLIC USE_NON_ATOMIC_PTR)
endif()

if (USE_PNG)
    target_compile_definitions(${IMCORELIB} PUBLIC USE_PNG)
    target_link_libraries(${IMCORELIB} libpng)
endif()

if (USE_JPEG)
    target_compile_definitions(${IMCORELIB} PUBLIC USE_JPEG)
    target_link_libraries(${IMCORELIB} libjpeg)
endif()

target_link_libraries(${IMCORELIB} imutil)

target_compile_definitions(${IMCORELIB} PUBLIC COLOR_DEPTH=${COLOR_DEPTH})     #color depth. (32/16 etc.)

if (COLOR_DEPTH EQUAL 16)
    # 16bpp: abgr1555, the only 16bpp model provided by color16.hpp
    target_compile_definitions(${IMCORELIB} PUBLIC ENDIAN=1555)             # color16 model selector
    target_compile_definitions(${IMCORELIB} PUBLIC RGB_MODEL=abgr)
else()
    if (MSVC OR WIN32)
        target_compile_definitions(${IMCORELIB} PUBLIC ENDIAN=le)                  #big/little endian. (le/be)
        target_compile_definitions(${IMCORELIB} PUBLIC RGB_MODEL=bgra32)           #rgb color model. (argb32/rgba32/rgba888/rgb565 etc.)
    elseif (APPLE)
        target_compile_definitions(${IMCORELIB} PUBLIC ENDIAN=le)                  #big/little endian. (le/be)
        target_compile_definitions(${IMCORELIB} PUBLIC RGB_MODEL=bgra32)           #rgb color model. (argb32/rgba32/rgba888/rgb565 etc.)
    elseif (UNIX)
        target_compile_definitions(${IMCORELIB} PUBLIC ENDIAN=be)                  #big/little endian. (le/be)
        target_compile_definitions(${IMCORELIB} PUBLIC RGB_MODEL=argb32)           #rgb color model. (argb32/rgba32/rgba888/rgb565 etc.)
    endif ()
endif ()
