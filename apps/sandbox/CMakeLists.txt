add_library(
    sandbox_app STATIC
    src/app_maker.cpp
    src/sandbox.cpp
)

target_link_libraries(
    sandbox_app
    PUBLIC
    imapp_canvas
)

target_include_directories(
    sandbox_app
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# runtime-TTF text (USE_TTF_RUNTIME): the web tier scales header/hint
# text with the buffer, which only set_font_size can do (code-contract
# 2.4). Pack the Inter blob like seq303 does; without the option the 5x7
# bitmap stays the default and nothing is generated (contract 2.4
# documented degradation)
if (USE_TTF_RUNTIME)
    set(SANDBOX_FONT_HEADER ${CMAKE_CURRENT_BINARY_DIR}/sandbox_font.gen.hpp)
    add_custom_command(
        OUTPUT ${SANDBOX_FONT_HEADER}
        COMMAND bytes_embed ${SANDBOX_FONT_HEADER} sandbox_font
                ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        DEPENDS bytes_embed ${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        VERBATIM)
    add_custom_target(sandbox_font_gen DEPENDS ${SANDBOX_FONT_HEADER})
    add_dependencies(sandbox_app sandbox_font_gen)
    target_include_directories(sandbox_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()
