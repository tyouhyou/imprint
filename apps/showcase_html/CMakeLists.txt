add_library(showcase_html_app STATIC src/app_maker.cpp src/showcase_html.cpp)
target_link_libraries(showcase_html_app PUBLIC imapp_canvas)
target_include_directories(showcase_html_app
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# runtime-TTF builds resolve per-widget font_size against the vendored
# Inter (SHOWCASE_HTML_FONT overrides at runtime); without the option
# the 5x7 bitmap stays the default and font-size stays ignored
if (USE_TTF_RUNTIME)
    target_compile_definitions(showcase_html_app PRIVATE
        IM_HTML_TTF_FONT="${CMAKE_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf")
endif()

# design document packed at build time (contract: build-time validation
# with the same parser; NDS has no filesystem, so the embedded bytes are
# the shipping form on every target). SHOWCASE_HTML_DOC overrides the
# document (probe builds, alternate designs).
set(SHOWCASE_HTML_DOC "${CMAKE_CURRENT_SOURCE_DIR}/space.html" CACHE FILEPATH
    "HTML design document embedded into the showcase_html app")
get_filename_component(SHOWCASE_HTML_NAME "${SHOWCASE_HTML_DOC}" NAME)
target_compile_definitions(showcase_html_app PRIVATE
    SHOWCASE_HTML_NAME="${SHOWCASE_HTML_NAME}")
set(SHOWCASE_HTML_HEADER ${CMAKE_CURRENT_BINARY_DIR}/showcase_html.gen.hpp)
if (NDS_PLATFORM)
    # the html_embed host tool does not build under the ARM toolchain; the
    # cross build must be handed a host-built executable instead of
    # skipping validation silently
    set(HTML_EMBED_EXECUTABLE "" CACHE FILEPATH
        "Host-built html_embed executable for embedded showcase_html builds")
    if (HTML_EMBED_EXECUTABLE STREQUAL "")
        message(FATAL_ERROR
            "showcase_html on NDS requires -DHTML_EMBED_EXECUTABLE=<path to a host-built html_embed>")
    endif()
    add_custom_command(
        OUTPUT ${SHOWCASE_HTML_HEADER}
        COMMAND ${HTML_EMBED_EXECUTABLE} ${SHOWCASE_HTML_HEADER} ${SHOWCASE_HTML_DOC}
        DEPENDS ${SHOWCASE_HTML_DOC}
        VERBATIM)
else()
    add_custom_command(
        OUTPUT ${SHOWCASE_HTML_HEADER}
        COMMAND html_embed ${SHOWCASE_HTML_HEADER} ${SHOWCASE_HTML_DOC}
        DEPENDS html_embed ${SHOWCASE_HTML_DOC}
        VERBATIM)
endif()
add_custom_target(showcase_html_gen DEPENDS ${SHOWCASE_HTML_HEADER})
add_dependencies(showcase_html_app showcase_html_gen)
target_include_directories(showcase_html_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
