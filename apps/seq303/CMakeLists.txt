add_library(
    seq303_app STATIC
    src/app_maker.cpp
    src/seq303.cpp
)

target_link_libraries(
    seq303_app
    PUBLIC
    imapp_canvas
)

target_include_directories(
    seq303_app
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# runtime-TTF text (USE_TTF_RUNTIME): the web tier scales header/hint
# text with the buffer, which only set_font_size can do (code-contract
# 2.4). Pack the Inter blob like showcase does; without the option the
# 5x7 bitmap stays the default and nothing is generated (contract 2.4
# documented degradation)
if (USE_TTF_RUNTIME)
    set(SEQ303_FONT_HEADER ${CMAKE_CURRENT_BINARY_DIR}/seq303_font.gen.hpp)
    add_custom_command(
        OUTPUT ${SEQ303_FONT_HEADER}
        COMMAND bytes_embed ${SEQ303_FONT_HEADER} seq303_font
                ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        DEPENDS bytes_embed ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        VERBATIM)
    add_custom_target(seq303_font_gen DEPENDS ${SEQ303_FONT_HEADER})
    add_dependencies(seq303_app seq303_font_gen)
    target_include_directories(seq303_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()
