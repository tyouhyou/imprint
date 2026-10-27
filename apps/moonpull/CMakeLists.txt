add_library(
    moonpull_app STATIC
    src/app_maker.cpp
    src/moonpull.cpp
)

# the 3D stories consume the optional raster module (zb::raster), which
# only exists under IMPRINT_WITH_IMRASTER -- the pixel-model gate the
# module configures itself under is the same one that guards this
# subdirectory in apps/CMakeLists.txt
target_link_libraries(
    moonpull_app
    PUBLIC
    imapp_canvas
    imraster
)

target_include_directories(
    moonpull_app
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# runtime-TTF text (USE_TTF_RUNTIME): the web tier scales HUD text with
# the buffer, which only set_font_size can do (code-contract 2.4). Pack
# the Inter blob like the other portal stories do; without the option
# the 5x7 bitmap stays the default and nothing is generated
if (USE_TTF_RUNTIME)
    set(MOONPULL_FONT_HEADER ${CMAKE_CURRENT_BINARY_DIR}/moonpull_font.gen.hpp)
    add_custom_command(
        OUTPUT ${MOONPULL_FONT_HEADER}
        COMMAND bytes_embed ${MOONPULL_FONT_HEADER} moonpull_font
                ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        DEPENDS bytes_embed ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        VERBATIM)
    add_custom_target(moonpull_font_gen DEPENDS ${MOONPULL_FONT_HEADER})
    add_dependencies(moonpull_app moonpull_font_gen)
    target_include_directories(moonpull_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()
