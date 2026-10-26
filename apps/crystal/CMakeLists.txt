add_library(
    crystal_app STATIC
    src/app_maker.cpp
    src/crystal.cpp
)

target_link_libraries(
    crystal_app
    PUBLIC
    imapp_canvas
)

target_include_directories(
    crystal_app
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# runtime-TTF text (USE_TTF_RUNTIME): the web tier scales HUD/card text
# with the buffer, which only set_font_size can do (code-contract 2.4).
# Pack the Inter blob like showcase does; without the option the 5x7
# bitmap stays the default and nothing is generated (contract 2.4
# documented degradation)
if (USE_TTF_RUNTIME)
    set(CRYSTAL_FONT_HEADER ${CMAKE_CURRENT_BINARY_DIR}/crystal_font.gen.hpp)
    add_custom_command(
        OUTPUT ${CRYSTAL_FONT_HEADER}
        COMMAND bytes_embed ${CRYSTAL_FONT_HEADER} crystal_font
                ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        DEPENDS bytes_embed ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        VERBATIM)
    add_custom_target(crystal_font_gen DEPENDS ${CRYSTAL_FONT_HEADER})
    add_dependencies(crystal_app crystal_font_gen)
    target_include_directories(crystal_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()
