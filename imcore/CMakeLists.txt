if (MSVC OR WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# COLOR_DEPTH / RGB_MODEL / ENDIAN are declared and resolved at the top
# level (A-29 Phase 1: the imraster module's configure gate consumes the
# same resolution); this file only consumes IMPRINT_RGB_MODEL / IMPRINT_ENDIAN.
# Every feature option (USE_PNG, USE_JPEG, USE_TTF_RUNTIME, USE_FONT_SIZE,
# FONT_SUBSET, TTF_FONT, USE_INTEGER_GEOMETRY, USE_NON_ATOMIC_PTR) is
# declared in cmake/imprint-features.cmake (A-28); this file consumes them.

# USE_FONT_SIZE with an empty TTF_FONT resolves the platform's system font
# (hardcoded paths); a missing file is a config error.
if (USE_FONT_SIZE AND NOT NDS_PLATFORM AND TTF_FONT STREQUAL "")
    if (MSVC OR WIN32)
        set(TTF_FONT "C:/Windows/Fonts/segoeui.ttf")
    elseif (APPLE)
        set(TTF_FONT "/System/Library/Fonts/Supplemental/Arial.ttf")
    elseif (UNIX)
        set(TTF_FONT "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
    endif()
    if (NOT TTF_FONT STREQUAL "" AND NOT EXISTS "${TTF_FONT}")
        message(FATAL_ERROR "USE_FONT_SIZE: platform default font not found: ${TTF_FONT} (set TTF_FONT to override)")
    endif()
endif()
if (USE_FONT_SIZE AND NDS_PLATFORM)
    message(WARNING "USE_FONT_SIZE is host-only; ignored on the NDS toolchain")
endif()

set(IMCORE_HAS_SUBSET OFF)
set(IMCORE_HAS_TTF_SUBSET OFF)
if (FONT_SUBSET OR NOT TTF_FONT STREQUAL "")
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if (Python3_Interpreter_FOUND)
        file(GLOB_RECURSE FONT_SUBSET_INPUTS CONFIGURE_DEPENDS
            ${PROJECT_SOURCE_DIR}/apps/*/src/*.cpp
            ${PROJECT_SOURCE_DIR}/apps/*/include/*.hpp
            ${PROJECT_SOURCE_DIR}/test/*.cpp
            ${PROJECT_SOURCE_DIR}/tools/examples/*.ui
            ${PROJECT_SOURCE_DIR}/apps/*/*.ui)
        file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/text)
    endif()
endif()

if (FONT_SUBSET)
    if (Python3_Interpreter_FOUND)
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
endif()

if (NOT TTF_FONT STREQUAL "" AND NOT NDS_PLATFORM)
    if (Python3_Interpreter_FOUND)
        add_executable(ttf_subset ${PROJECT_SOURCE_DIR}/tools/ttf_subset.cpp)
        # SYSTEM: the build tool includes vendored stb_truetype directly
        target_include_directories(ttf_subset SYSTEM PRIVATE
            ${PROJECT_SOURCE_DIR}/third_party/stb)
        set(TTF_CODEPOINTS ${CMAKE_CURRENT_BINARY_DIR}/ttf_codepoints.txt)
        set(TTF_GLYPHS ${CMAKE_CURRENT_BINARY_DIR}/include/text/ttf_glyphs.hpp)
        # the rasterizer writes into this directory; a fresh build tree
        # must have it before the custom command runs (the FONT_SUBSET
        # branch makes the same call for the same reason)
        file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/text)
        add_custom_command(
            OUTPUT ${TTF_GLYPHS}
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../tools/font_subset.py
                --codepoints-out ${TTF_CODEPOINTS}
                ${FONT_SUBSET_INPUTS}
            COMMAND ttf_subset ${TTF_FONT} ${TTF_PIXEL_SIZE} ${TTF_CODEPOINTS} ${TTF_GLYPHS}
            DEPENDS ttf_subset ${FONT_SUBSET_INPUTS} ${TTF_FONT}
            VERBATIM)
        # the generated header listed as a source guarantees the custom
        # command runs before the provider compiles (all generators)
        set(TTF_SRC src/text/ttf_provider.cpp ${TTF_GLYPHS})
        set(IMCORE_HAS_TTF_SUBSET ON)
    else()
        message(WARNING "TTF_FONT is set but Python 3 was not found; the TTF subset stays off")
    endif()
endif()

if (NOT IMCORE_HAS_TTF_SUBSET)
    set(TTF_SRC)
endif()
# runtime TTF rasterization (batch L-5): vendored stb_truetype behind one
# wrapper TU; with the option off none of it compiles (contract 2.4)
if (USE_TTF_RUNTIME)
    set(TTF_RUNTIME_SRC
        src/text/stb_truetype_impl.cpp
        src/text/runtime_ttf_provider.cpp
    )
else()
    set(TTF_RUNTIME_SRC)
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
# hand-written GIF writer: dependency-free, always available (V-0)
set(GIF_SRC
    src/codec/gif.cpp
)

add_library(
    ${IMCORELIB} SHARED
    src/core/graphics.cpp
    src/core/pixel_convert.cpp
    src/text/text_image.cpp
    src/text/bitmap_provider.cpp
    src/text/utf8.cpp
    ${TTF_SRC}
    ${TTF_RUNTIME_SRC}
    ${STB_SRC}
    ${JPEG_SRC}
    ${PNG_SRC}
    ${GIF_SRC}
)

# X-14: the kernel is deliberately SHARED (A-4.2: dynamic hosts load it),
# and a shared library without a SOVERSION installs as a bare
# libimcore.so that an in-place upgrade silently replaces for every
# binary already linked against it. Pin the ABI version: the SONAME
# tracks the project's major version, so an ABI break requires the major
# bump that a package consumer's find_package(imprint <ver>) also sees.
if (NOT NDS_PLATFORM)
    set_target_properties(${IMCORELIB} PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR})
endif()

target_include_directories(
    ${IMCORELIB}

    PUBLIC
    # P1.5 install/export: build tree keeps the source path; the
    # installed tree reproduces the module's -I root under
    # include/imprint/imcore/include
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/imprint/imcore/include>

    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core
    ${CMAKE_CURRENT_SOURCE_DIR}/include/text
    ${CMAKE_CURRENT_SOURCE_DIR}/include/codec
)

# vendored stb headers are third-party sources: SYSTEM keeps them out of
# the project warning set (-Werror applies to our code only)
target_include_directories(${IMCORELIB} SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/stb
)

if (USE_PNG)
    target_compile_definitions(${IMCORELIB} PUBLIC USE_PNG)
endif()

if (USE_NON_ATOMIC_PTR)
    target_compile_definitions(${IMCORELIB} PUBLIC USE_NON_ATOMIC_PTR)
endif()

if (IMCORE_HAS_SUBSET)
    # the generated table lives in the build dir; PUBLIC so the tests can
    # branch on the same macro the provider was compiled with
    target_compile_definitions(${IMCORELIB} PUBLIC IMCORE_HAS_SUBSET)
    target_include_directories(${IMCORELIB} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include/text>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>)
endif()

if (IMCORE_HAS_TTF_SUBSET)
    # the rasterized table (plan 2) follows the same pattern as the 5x7
    # subset: PUBLIC macro + generated include dir
    target_compile_definitions(${IMCORELIB} PUBLIC IMCORE_HAS_TTF_SUBSET)
    target_include_directories(${IMCORELIB} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include/text>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>)
endif()

if (USE_FONT_SIZE)
    if (IMCORE_HAS_TTF_SUBSET)
        # PUBLIC so imui/CanvasWindow and the tests compile against the
        # same macro the provider was built with
        target_compile_definitions(${IMCORELIB} PUBLIC IMCORE_USE_FONT_SIZE)
    else()
        message(WARNING "USE_FONT_SIZE is ON but the TTF subset pipeline did not run; the 5x7 bitmap stays the default")
    endif()
endif()

if (USE_TTF_RUNTIME)
    # PUBLIC so imui/CanvasWindow and the tests compile against the same
    # macro the provider was built with (the USE_FONT_SIZE pattern)
    target_compile_definitions(${IMCORELIB} PUBLIC IMCORE_HAS_TTF_RUNTIME)
endif()

if (USE_JPEG)
    target_compile_definitions(${IMCORELIB} PUBLIC USE_JPEG)
endif()

target_link_libraries(${IMCORELIB} imutil)

target_compile_definitions(${IMCORELIB} PUBLIC COLOR_DEPTH=${COLOR_DEPTH})     #color depth. (32/16 etc.)
target_compile_definitions(${IMCORELIB} PUBLIC ENDIAN=${IMPRINT_ENDIAN})       #big/little endian. (le/be)
target_compile_definitions(${IMCORELIB} PUBLIC RGB_MODEL=${IMPRINT_RGB_MODEL}) #rgb color model. (argb32/rgba32/rgba888/rgb565 etc.)
