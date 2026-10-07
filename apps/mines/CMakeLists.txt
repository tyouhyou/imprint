add_library(
    mines_app STATIC
    src/app_maker.cpp
    src/mines.cpp
)

target_link_libraries(
    mines_app
    PUBLIC
    imapp_canvas
)

target_include_directories(
    mines_app
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# runtime-TTF text (USE_TTF_RUNTIME): the web tier scales header/dialog
# text with the buffer, which only set_font_size can do (code-contract
# 2.4). Pack the Inter blob like g2048 does; without the option the 5x7
# bitmap stays the default and nothing is generated (contract 2.4
# documented degradation)
if (USE_TTF_RUNTIME)
    set(MINES_FONT_HEADER ${CMAKE_CURRENT_BINARY_DIR}/mines_font.gen.hpp)
    add_custom_command(
        OUTPUT ${MINES_FONT_HEADER}
        COMMAND bytes_embed ${MINES_FONT_HEADER} mines_font
                ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        DEPENDS bytes_embed ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        VERBATIM)
    add_custom_target(mines_font_gen DEPENDS ${MINES_FONT_HEADER})
    add_dependencies(mines_app mines_font_gen)
    target_include_directories(mines_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()
