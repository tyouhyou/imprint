# the tests run on the host; they are not built for embedded targets
if (NOT NDS_PLATFORM)

add_executable(
    test_imui
    test_main.cpp
    test_dirty.cpp
    test_builder.cpp
    test_panel.cpp
    test_text_input.cpp
    test_button.cpp
    test_label.cpp
    test_dialog.cpp
    test_dispatch.cpp
    test_focus.cpp
    test_canvas_window.cpp
    test_board.cpp
    test_game.cpp
    test_ai.cpp
    test_blit_font.cpp
    test_app_flow.cpp
    test_graphics.cpp
    test_event.cpp
    test_text.cpp
    test_flex.cpp
    test_ptr.cpp
    test_codec.cpp
    test_checkbox.cpp
    test_radio.cpp
    test_slider.cpp
    test_list_box.cpp
    test_measure.cpp
    test_ui_file.cpp
    test_widget_size.cpp
)

target_link_libraries(
    test_imui
    PRIVATE
    imui
    imapp
    imutil
    tictactoe_app
)

# build-time packing smoke: ui_embed validates and embeds the example
# document; test_ui_file parses the packed bytes back
set(UI_EMBED_TEST_HEADER ${CMAKE_CURRENT_BINARY_DIR}/ui_embed_test.gen.hpp)
add_custom_command(
    OUTPUT ${UI_EMBED_TEST_HEADER}
    COMMAND ui_embed ${UI_EMBED_TEST_HEADER} ${CMAKE_SOURCE_DIR}/tools/examples/menu.ui
    DEPENDS ui_embed ${CMAKE_SOURCE_DIR}/tools/examples/menu.ui
    VERBATIM
)
add_custom_target(ui_embed_test_gen DEPENDS ${UI_EMBED_TEST_HEADER})
add_dependencies(test_imui ui_embed_test_gen)
target_include_directories(test_imui PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

endif()
