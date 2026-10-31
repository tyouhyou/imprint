# A-28: the declarative front-ends are the gated part of the imui closure
# (inventory: ARCHITECTURE §4.9). A feature off swaps the parser TU for a
# stub that rejects every document, so the header API stays link-complete
# and the degradation is a logged init-path failure (code-contract §5.4).
set(IMUI_FRONTEND_SRC)
if (IMPRINT_FEATURE_UI_FILE)
    list(APPEND IMUI_FRONTEND_SRC src/ui_file.cpp)
else()
    list(APPEND IMUI_FRONTEND_SRC src/ui_file_stub.cpp)
endif()
if (IMPRINT_FEATURE_HTML)
    list(APPEND IMUI_FRONTEND_SRC src/html.cpp)
else()
    list(APPEND IMUI_FRONTEND_SRC src/html_stub.cpp)
endif()

add_library(
    ${IMUILIB} STATIC
    src/widget.cpp
    src/panel.cpp
    src/flex_panel.cpp
    src/scroll_panel.cpp
    src/button.cpp
    src/checkbox.cpp
    src/label.cpp
    src/radio_button.cpp
    src/slider.cpp
    src/progress_bar.cpp
    src/list_box.cpp
    src/theme.cpp
    src/ui_builder.cpp
    ${IMUI_FRONTEND_SRC}
    src/text_input.cpp
    src/dialog.cpp
    src/dispatcher.cpp
    src/toggle_switch.cpp
    src/gauge_dial.cpp
    src/knob.cpp
    src/trend_line.cpp
    src/svg_canvas.cpp
)

# PUBLIC 0/1 (not presence-only) so consumers and tests can branch on the
# same macro the library was built with -- the IMCORE_HAS_* pattern, except
# the parser symbols always exist
if (IMPRINT_FEATURE_HTML)
    target_compile_definitions(${IMUILIB} PUBLIC IMUI_HAS_HTML=1)
else()
    target_compile_definitions(${IMUILIB} PUBLIC IMUI_HAS_HTML=0)
endif()
if (IMPRINT_FEATURE_UI_FILE)
    target_compile_definitions(${IMUILIB} PUBLIC IMUI_HAS_UI_FILE=1)
else()
    target_compile_definitions(${IMUILIB} PUBLIC IMUI_HAS_UI_FILE=0)
endif()

target_include_directories(
    ${IMUILIB}
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/imprint/imui/include>
)

target_link_libraries(
    ${IMUILIB}
    PUBLIC
    ${IMCORELIB}
    ${IMEVENTLIB}
    ${IMINPUTLIB}
)
