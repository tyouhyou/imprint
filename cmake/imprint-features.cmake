# A-28: the post-core feature inventory's build gates, in one place.
#
# ARCHITECTURE §4.9 carries the inventory (sources, dependency DAG, gate vs.
# static dead-strip, C-ABI impact, measured sizes); this file carries the
# declarations. No other file declares one of these options (the pixel
# matrix and the warning gate keep their own homes); the modules consume
# these variables, a toolchain FORCEs only the pins it owns beside its other
# pins (cmake/nds.toolchain.cmake: the imcore USE_* pair and
# IMPRINT_WITH_IMRASTER -- the front-end gates need no pin, default ON and a
# static NDS link dead-strips what no story references), and the
# default build is exactly the zero-option full-feature build it always was.
#
# Two families live here:
#   * packaging bundles (IMPRINT_WITH_*, configure-scope, ARCHITECTURE §3.2)
#   * feature options: the USE_* family compiled into the SHARED imcore, and
#     the IMPRINT_FEATURE_* front-ends compiled into the STATIC imui.
# A feature off must not silently change a public surface: the C-ABI and the
# imui parser headers stay link-complete (a stub replaces the parser), so a
# host linking against a feature-off build fails at runtime with a logged
# diagnostic, never at link time (docs/code-contract.md §5.4).

# ---------------------------------------------------------------- bundles --
# A-23 / Batch G P1: configure-time packaging gates. The options state
# standalone intent; CMakeLists.txt resolves the effective switches (the test
# battery needs the demo app libraries and the ui_embed tool, the demo
# composition implies both).
option(IMPRINT_WITH_TOOLS "Also build the host tools (ui_embed, html_embed, asset_gen)" ${IMPRINT_IS_TOP_LEVEL})
option(IMPRINT_WITH_TESTS "Build the test battery (implies the demo app libraries and the host tools)" ${IMPRINT_IS_TOP_LEVEL})
option(IMPRINT_WITH_DEMOS "Build the demo app libraries, the binding, and the STORY demo executable (framework mode)" ${IMPRINT_IS_TOP_LEVEL})
# X-3: the shell is packaging, not part of the library surface -- a headless
# consumer of imcore/imui/imapp must not inherit libx11-dev. ON top level.
option(IMPRINT_WITH_SHELL "Build the platform shell (A-2 seams + the selected backend's usage requirements)" ${IMPRINT_IS_TOP_LEVEL})
# the optional 3D raster module (A-29 / ARCHITECTURE §4.12); the module's own
# CMakeLists enforces the pixel-model gate
option(IMPRINT_WITH_IMRASTER "Build the optional 3D raster module (zb::raster; 32bpp rgba32/le pixel model only, ARCHITECTURE §4.12)" OFF)

# ------------------------------------------------------- imui front-ends --
# The declarative front-ends are leaves of the imui source closure (only the
# C-ABI declarative entry and the design-file tools reference them), so a
# STATIC final link drops them -- but two links cannot: a SHARED consumer of
# imui (libzbapi) and the hand-written wasm TU list link every TU, and the
# exported zb_app_create_from_ui keeps the declarative closure reachable.
# The measured size win that met the A-28 trigger, and the rule that the
# export list itself is not a gate, live in ARCHITECTURE §4.9.1.
option(IMPRINT_FEATURE_HTML "Build the HTML/CSS front-end (imui/src/html.cpp; docs/html-path.md)" ON)
option(IMPRINT_FEATURE_UI_FILE "Build the .ui design-file front-end (imui/src/ui_file.cpp)" ON)

# story/target-level closure (A-28 layer 3): a target whose sources call a
# front-end declares the need here, so a feature-off configure names the
# target instead of failing later with an unresolved parser (or, worse,
# materializing an empty document at runtime).
function(imprint_require_feature feature)
    if (NOT IMPRINT_FEATURE_${feature})
        message(FATAL_ERROR
            "${CMAKE_CURRENT_SOURCE_DIR} requires IMPRINT_FEATURE_${feature}; "
            "configure with -DIMPRINT_FEATURE_${feature}=ON (ARCHITECTURE §4.9)")
    endif()
endfunction()

# ---------------------------------------------------- imcore features ----
# COLOR_DEPTH / RGB_MODEL / ENDIAN are the pixel matrix (ARCHITECTURE §4.4/
# §4.9), not features, and stay declared at the top level.
#
# USE_PNG / USE_JPEG default OFF (V-4's row in the §4.9 inventory): compiled
# into the SHARED imcore, where the linker cannot drop them -- the measured
# libimcore.so delta is in ARCHITECTURE §4.9.1 (A-28 survey, 2026-10-31).
# The codecs are self-contained: they compile the vendored
# stb_image/stb_image_write (third_party/stb) and need no external library.
set(USE_PNG OFF CACHE BOOL "Switch it to ON if png file processing is needed.")
set(USE_JPEG OFF CACHE BOOL "Switch it to ON if jpeg file processing is needed.")
# Integer-only geometry: no std::sqrt in the circle/ellipse scanlines and no
# runtime floating point in the arc trig for FPU-less embedded targets (the
# NDS toolchain sets this automatically).
set(USE_INTEGER_GEOMETRY OFF CACHE BOOL "Switch it to ON for FPU-less embedded targets (integer-only circle/ellipse bounds + arc trig lookup).")
# Non-atomic shared pointer: zb::SharedPtr falls back to a plain-int refcount
# for targets without atomic instructions (devkitARM's NDS ships no
# libatomic; the toolchain sets this automatically). The UI is single-threaded.
set(USE_NON_ATOMIC_PTR OFF CACHE BOOL "Switch it to ON for targets without atomic instructions (non-atomic refcounts).")
# Font subset (batch E): scans the app/test sources at configure time and
# generates a glyph table for the code units they use beyond ASCII
# (tools/font_subset.py + tools/extra_glyphs.py). Needs Python 3.
set(FONT_SUBSET ON CACHE BOOL "Scan sources and generate the font subset table (needs Python 3).")
# Font subset plan 2 (batch S2, code-contract 2.4): rasterize a TrueType font
# into the subset at build time (tools/ttf_subset over the vendored
# stb_truetype). Empty TTF_FONT = off. Host builds only, like ui_embed.
set(TTF_FONT "" CACHE FILEPATH "TrueType font rasterized into the build-time glyph subset (plan 2). Empty = off.")
set(TTF_PIXEL_SIZE 16 CACHE STRING "Pixel size of the rasterized TTF subset (plan 2).")
# Platform default font size (code-contract 2.4): USE_FONT_SIZE resolves an
# empty TTF_FONT to the platform's system font and installs the rasterized
# subset as the default glyph provider. Hosts only -- never on NDS.
set(USE_FONT_SIZE OFF CACHE BOOL "Render widgets with the platform default font at TTF_PIXEL_SIZE instead of the 5x7 bitmap (hosts only).")
# Runtime TTF rasterization (batch L-5): vendored stb_truetype behind one
# wrapper TU (measured cost: ARCHITECTURE §4.9.1).
set(USE_TTF_RUNTIME OFF CACHE BOOL "Runtime TTF rasterization through vendored stb_truetype (batch L-5): TtfFamily hands out one glyph provider per pixel size over a bounded shared cache. When OFF only the 5x7 bitmap and build-time subset paths exist.")
