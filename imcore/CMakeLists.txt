if (MSVC OR WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# 16bpp is meant for embedded displays (NDS). The desktop shells assume
# 32bpp (e.g. the Win32 DIB blit), so COLOR_DEPTH=16 builds are only
# supported on embedded targets.
set(COLOR_DEPTH 32 CACHE STRING "Pixel color depth in bits (16 or 32). Set 16 for embedded framebuffers only.")
# NOTE: these options are OFF in production builds and the default build
# stays zero-dependency. USE_FONT links FreeType through hardcoded
# per-platform paths below (verified on Linux with the distro dev package
# and Windows with the prebuilt DLLs under ext_lib); before enabling it
# elsewhere, prefer find_package()/CACHE variables over the hardcoded
# IMPORTED_LOCATION entries. USE_PNG and USE_JPEG are self-contained: they
# compile the vendored stb_image/stb_image_write (third_party/stb) into
# imcore and need no external library.
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
# Font subset (batch E): scans the app/test sources at configure time and
# generates a glyph table for the code units they use beyond the built-in
# ASCII range (tools/font_subset.py + tools/extra_glyphs.py). Needs
# Python 3; without it the provider falls back to ASCII-only glyphs.
set(FONT_SUBSET ON CACHE BOOL "Scan sources and generate the font subset table (needs Python 3).")
if (FONT_SUBSET)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    set(IMCORE_HAS_SUBSET OFF)
    if (Python3_Interpreter_FOUND)
        file(GLOB_RECURSE FONT_SUBSET_INPUTS CONFIGURE_DEPENDS
            ${CMAKE_SOURCE_DIR}/apps/*/src/*.cpp
            ${CMAKE_SOURCE_DIR}/apps/*/include/*.hpp
            ${CMAKE_SOURCE_DIR}/test/*.cpp)
        file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/text)
        execute_process(
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../tools/font_subset.py
                --extras ${CMAKE_CURRENT_SOURCE_DIR}/../tools/extra_glyphs.py
                --out ${CMAKE_CURRENT_BINARY_DIR}/include/text/subset_glyphs.hpp
                ${FONT_SUBSET_INPUTS}
            RESULT_VARIABLE FONT_SUBSET_RESULT)
        if (NOT FONT_SUBSET_RESULT EQUAL 0)
            message(WARNING "font subset generation failed (code ${FONT_SUBSET_RESULT}); falling back to ASCII-only glyphs")
        else()
            set(IMCORE_HAS_SUBSET ON)
        endif()
    else()
        message(STATUS "Python 3 not found: font subset generation disabled (ASCII-only glyphs)")
    endif()
else()
    set(IMCORE_HAS_SUBSET OFF)
endif()

if (USE_FONT)
    set(FONT_SRC
        src/text/font.cpp
    )
else()
    set(FONT_SRC)
endif()
# vendored single-file codecs: one implementation TU, compiled only when a
# codec option is on (png.cpp/jpeg.cpp, the Image API, use stb internally)
if (USE_PNG OR USE_JPEG)
    set(STB_SRC
        src/codec/stb_impl.cpp
    )
else()
    set(STB_SRC)
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
    ${STB_SRC}
    ${JPEG_SRC}
    ${PNG_SRC}
)

if (USE_FONT)
    # hardcoded per-platform library paths (see the note at USE_FONT)
add_library(freetype2 SHARED IMPORTED GLOBAL)
endif()

if(APPLE)
if (USE_FONT)
    set_target_properties(
        freetype2 PROPERTIES 
        IMPORTED_LOCATION /usr/X11/lib/libfreetype.dylib
    )
endif()
elseif(UNIX)
if (USE_FONT)
    set_target_properties(
        freetype2 PROPERTIES 
        IMPORTED_LOCATION /usr/lib/x86_64-linux-gnu/libfreetype.so
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
endif()

if(APPLE)
set (INC_DIRS
    /Developer/SDKs/MacOSX10.6.sdk/usr/X11/include
    /Developer/SDKs/MacOSX10.6.sdk/usr/X11/include/freetype2
    /Developer/SDKs/MacOSX10.6.sdk/usr/X11/include/freetype2/freetype)
elseif(UNIX)
set (INC_DIRS
    /usr/include/freetype2
    /usr/include/freetype2/freetype)
elseif(MSVC OR WIN32)
set (INC_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}\\ext_lib\\freetype\\include
    ${CMAKE_CURRENT_SOURCE_DIR}\\ext_lib\\freetype\\include\\freetype)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/stb
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

if (IMCORE_HAS_SUBSET)
    # the generated table lives in the build dir; PUBLIC so the tests can
    # branch on the same macro the provider was compiled with
    target_compile_definitions(${IMCORELIB} PUBLIC IMCORE_HAS_SUBSET)
    target_include_directories(${IMCORELIB} PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}/include/text ${CMAKE_CURRENT_BINARY_DIR}/include)
endif()

if (USE_PNG)
    target_compile_definitions(${IMCORELIB} PUBLIC USE_PNG)
endif()

if (USE_JPEG)
    target_compile_definitions(${IMCORELIB} PUBLIC USE_JPEG)
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
