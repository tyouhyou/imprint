add_library(
    g2048_app STATIC
    src/app_maker.cpp
    src/g2048.cpp
)

target_link_libraries(
    g2048_app
    PUBLIC
    imapp_canvas
)

target_include_directories(
    g2048_app
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# runtime-TTF text (USE_TTF_RUNTIME): the web tier scales header/dialog
# text with the buffer, which only set_font_size can do (code-contract
# 2.4). Pack the Inter blob like showcase does; without the option the
# 5x7 bitmap stays the default and nothing is generated (contract 2.4
# documented degradation)
if (USE_TTF_RUNTIME)
    set(G2048_FONT_HEADER ${CMAKE_CURRENT_BINARY_DIR}/g2048_font.gen.hpp)
    add_custom_command(
        OUTPUT ${G2048_FONT_HEADER}
        COMMAND bytes_embed ${G2048_FONT_HEADER} g2048_font
                ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        DEPENDS bytes_embed ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        VERBATIM)
    add_custom_target(g2048_font_gen DEPENDS ${G2048_FONT_HEADER})
    add_dependencies(g2048_app g2048_font_gen)
    target_include_directories(g2048_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()
