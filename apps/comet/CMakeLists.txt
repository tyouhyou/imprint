add_library(
    comet_app STATIC
    src/app_maker.cpp
    src/comet.cpp
)

target_link_libraries(
    comet_app
    PUBLIC
    imapp_canvas
)

target_include_directories(
    comet_app
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# runtime-TTF text (USE_TTF_RUNTIME): the web tier scales HUD/card text
# with the buffer, which only set_font_size can do (code-contract 2.4).
# Pack the Inter blob like showcase does; without the option the 5x7
# bitmap stays the default and nothing is generated (contract 2.4
# documented degradation)
if (USE_TTF_RUNTIME)
    set(COMET_FONT_HEADER ${CMAKE_CURRENT_BINARY_DIR}/comet_font.gen.hpp)
    add_custom_command(
        OUTPUT ${COMET_FONT_HEADER}
        COMMAND bytes_embed ${COMET_FONT_HEADER} comet_font
                ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        DEPENDS bytes_embed ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        VERBATIM)
    add_custom_target(comet_font_gen DEPENDS ${COMET_FONT_HEADER})
    add_dependencies(comet_app comet_font_gen)
    target_include_directories(comet_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()
