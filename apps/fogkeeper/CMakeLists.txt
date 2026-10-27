add_library(
    fogkeeper_app STATIC
    src/app_maker.cpp
    src/fogkeeper.cpp
)

# the 3D stories consume the optional raster module (zb::raster), which
# only exists under IMPRINT_WITH_IMRASTER -- the pixel-model gate the
# module configures itself under is the same one that guards this
# subdirectory in apps/CMakeLists.txt
target_link_libraries(
    fogkeeper_app
    PUBLIC
    imapp_canvas
    imraster
)

target_include_directories(
    fogkeeper_app
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# runtime-TTF text (USE_TTF_RUNTIME): the web tier scales HUD text with
# the buffer, which only set_font_size can do (code-contract 2.4). Pack
# the Inter blob like the other portal stories do; without the option
# the 5x7 bitmap stays the default and nothing is generated
if (USE_TTF_RUNTIME)
    set(FOGKEEPER_FONT_HEADER ${CMAKE_CURRENT_BINARY_DIR}/fogkeeper_font.gen.hpp)
    add_custom_command(
        OUTPUT ${FOGKEEPER_FONT_HEADER}
        COMMAND bytes_embed ${FOGKEEPER_FONT_HEADER} fogkeeper_font
                ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        DEPENDS bytes_embed ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        VERBATIM)
    add_custom_target(fogkeeper_font_gen DEPENDS ${FOGKEEPER_FONT_HEADER})
    add_dependencies(fogkeeper_app fogkeeper_font_gen)
    target_include_directories(fogkeeper_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()
