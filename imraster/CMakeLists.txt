# imraster: the optional software-3D raster core (zb::raster), absorbed
# verbatim from the fps validation consumer (A-29 Phase 1 / R20). The
# contracts are ARCHITECTURE.md §4.12 and code-contract.md §13; the
# module's own README carries the upstream boundary rules. The module's
# arithmetic is frozen upstream (the fps golden frames are the regression
# baseline) -- never edit src/*.cpp or include/imraster/*.hpp here.
# the option declaration lives in cmake/imprint-features.cmake (A-28)
if (NOT IMPRINT_WITH_IMRASTER)
    return()
endif()

# The packed-word arithmetic bakes the pixel model in: pixel.hpp
# static_asserts 32bpp rgba32/le (r in the low byte, a in the high byte --
# the src-over fast paths address the word lanes directly), and the
# configure gate enforces the same condition here. Every other pixel
# model configures the module out (ARCHITECTURE §4.12).
if (NOT COLOR_DEPTH EQUAL 32 OR NOT IMPRINT_RGB_MODEL STREQUAL "rgba32" OR NOT IMPRINT_ENDIAN STREQUAL "le")
    message(FATAL_ERROR
        "IMPRINT_WITH_IMRASTER needs the 32bpp rgba32/le pixel model "
        "(resolved: COLOR_DEPTH=${COLOR_DEPTH} RGB_MODEL=${IMPRINT_RGB_MODEL} "
        "ENDIAN=${IMPRINT_ENDIAN}); configure with -DRGB_MODEL=rgba32 -DENDIAN=le")
endif()

add_library(imraster STATIC
    src/canvas.cpp
    src/raster.cpp
    src/mesh.cpp
)
target_include_directories(imraster PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(imraster PUBLIC imcore)
