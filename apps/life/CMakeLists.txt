add_library(
    life_app STATIC
    src/app_maker.cpp
    src/life.cpp
)

target_link_libraries(
    life_app
    PUBLIC
    imapp_canvas
)

target_include_directories(
    life_app
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# runtime-TTF text (USE_TTF_RUNTIME): the web tier scales header/label
# text with the buffer, which only set_font_size can do (code-contract
# 2.4). Pack the Inter blob like g2048 does; without the option the 5x7
# bitmap stays the default and nothing is generated (contract 2.4
# documented degradation)
if (USE_TTF_RUNTIME)
    set(LIFE_FONT_HEADER ${CMAKE_CURRENT_BINARY_DIR}/life_font.gen.hpp)
    add_custom_command(
        OUTPUT ${LIFE_FONT_HEADER}
        COMMAND bytes_embed ${LIFE_FONT_HEADER} life_font
                ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        DEPENDS bytes_embed ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        VERBATIM)
    add_custom_target(life_font_gen DEPENDS ${LIFE_FONT_HEADER})
    add_dependencies(life_app life_font_gen)
    target_include_directories(life_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()
