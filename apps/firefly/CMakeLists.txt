add_library(
    firefly_app STATIC
    src/app_maker.cpp
    src/firefly.cpp
)

target_link_libraries(
    firefly_app
    PUBLIC
    imapp_canvas
)

target_include_directories(
    firefly_app
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# runtime-TTF text (USE_TTF_RUNTIME): the web tier scales HUD/card text
# with the buffer, which only set_font_size can do (code-contract 2.4).
# Pack the Inter blob like showcase does; without the option the 5x7
# bitmap stays the default and nothing is generated (contract 2.4
# documented degradation)
if (USE_TTF_RUNTIME)
    set(FIREFLY_FONT_HEADER ${CMAKE_CURRENT_BINARY_DIR}/firefly_font.gen.hpp)
    add_custom_command(
        OUTPUT ${FIREFLY_FONT_HEADER}
        COMMAND bytes_embed ${FIREFLY_FONT_HEADER} firefly_font
                ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        DEPENDS bytes_embed ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        VERBATIM)
    add_custom_target(firefly_font_gen DEPENDS ${FIREFLY_FONT_HEADER})
    add_dependencies(firefly_app firefly_font_gen)
    target_include_directories(firefly_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()
