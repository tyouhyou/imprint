# SIGNAL-ONE showcase (redesigned 2026-09-26): the UI is one HTML
# design file materialized through the declarative layer; behavior in
# showcase.cpp. Build-time steps follow the showcase_html precedent:
# html_embed packs (and validates) the design document, bytes_embed
# packs the Inter TTF blob for the runtime-TTF text path.
add_library(showcase_app STATIC src/app_maker.cpp src/showcase.cpp)
target_link_libraries(showcase_app PUBLIC imapp_canvas)
target_include_directories(showcase_app
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# the design document: build-time validation with the same parser
# (contract 5.3); NDS cross builds hand in a host-built html_embed
set(SHOWCASE_DOC "${CMAKE_CURRENT_SOURCE_DIR}/signal.html")
set(SHOWCASE_HTML_HEADER ${CMAKE_CURRENT_BINARY_DIR}/showcase_signal.gen.hpp)
if (NDS_PLATFORM)
    set(HTML_EMBED_EXECUTABLE "" CACHE FILEPATH
        "Host-built html_embed executable for embedded showcase builds")
    if (HTML_EMBED_EXECUTABLE STREQUAL "")
        message(FATAL_ERROR
            "showcase on NDS requires -DHTML_EMBED_EXECUTABLE=<path to a host-built html_embed>")
    endif()
    add_custom_command(
        OUTPUT ${SHOWCASE_HTML_HEADER}
        COMMAND ${HTML_EMBED_EXECUTABLE} ${SHOWCASE_HTML_HEADER} ${SHOWCASE_DOC}
        DEPENDS ${SHOWCASE_DOC}
        VERBATIM)
else()
    add_custom_command(
        OUTPUT ${SHOWCASE_HTML_HEADER}
        COMMAND html_embed ${SHOWCASE_HTML_HEADER} ${SHOWCASE_DOC}
        DEPENDS html_embed ${SHOWCASE_DOC}
        VERBATIM)
endif()
add_custom_target(showcase_signal_gen DEPENDS ${SHOWCASE_HTML_HEADER})
add_dependencies(showcase_app showcase_signal_gen)
target_include_directories(showcase_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# the Inter blob for runtime-TTF text (USE_TTF_RUNTIME): packed on every
# target, consumed only under IMCORE_HAS_TTF_RUNTIME. NDS cross builds
# hand in a host-built bytes_embed like html_embed above.
set(SHOWCASE_FONT_HEADER ${CMAKE_CURRENT_BINARY_DIR}/showcase_font.gen.hpp)
if (NDS_PLATFORM)
    set(BYTES_EMBED_EXECUTABLE "" CACHE FILEPATH
        "Host-built bytes_embed executable for embedded showcase builds")
    if (BYTES_EMBED_EXECUTABLE STREQUAL "")
        message(FATAL_ERROR
            "showcase on NDS requires -DBYTES_EMBED_EXECUTABLE=<path to a host-built bytes_embed>")
    endif()
    add_custom_command(
        OUTPUT ${SHOWCASE_FONT_HEADER}
        COMMAND ${BYTES_EMBED_EXECUTABLE} ${SHOWCASE_FONT_HEADER} showcase_font
                ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        DEPENDS ${BYTES_EMBED_EXECUTABLE} ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        VERBATIM)
else()
    add_custom_command(
        OUTPUT ${SHOWCASE_FONT_HEADER}
        COMMAND bytes_embed ${SHOWCASE_FONT_HEADER} showcase_font
                ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        DEPENDS bytes_embed ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        VERBATIM)
endif()
add_custom_target(showcase_font_gen DEPENDS ${SHOWCASE_FONT_HEADER})
add_dependencies(showcase_app showcase_font_gen)

if (NOT IMCORE_HAS_TTF_RUNTIME)
    # intended look needs proportional text; the build stays green with
    # the 5x7 fallback (contract 2.4 documented degradation)
    message(WARNING "showcase: configure -DUSE_TTF_RUNTIME=ON for the intended proportional-text look")
endif()

if (NOT NDS_PLATFORM)
    # S4 frame recorder: drives the app through the public API and dumps
    # the frames as an animated GIF (the README showcase asset); the
    # encoder is tool-local, not part of the shipping app library
    add_executable(showcase_gif src/gif_record.cpp)  # the encoder lives in imcore/codec (V-0)
    target_link_libraries(showcase_gif PRIVATE showcase_app)
endif()
