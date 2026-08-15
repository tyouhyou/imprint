add_library(
    ${IMUILIB} STATIC
    src/widget.cpp
    src/panel.cpp
    src/flex_panel.cpp
    src/button.cpp
    src/checkbox.cpp
    src/label.cpp
    src/radio_button.cpp
    src/slider.cpp
    src/list_box.cpp
    src/ui_builder.cpp
    src/ui_file.cpp
    src/text_input.cpp
    src/dialog.cpp
    src/dispatcher.cpp
)

target_include_directories(
    ${IMUILIB}
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
    ${IMUILIB}
    PUBLIC
    ${IMCORELIB}
    ${IMEVENTLIB}
    ${IMINPUTLIB}
)
