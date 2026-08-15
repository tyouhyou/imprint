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
)

target_link_libraries(
    test_imui
    PRIVATE
    imui
    imapp
    imutil
    tictactoe_app
)

endif()
