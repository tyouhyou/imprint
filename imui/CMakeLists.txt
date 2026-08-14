add_library(
    ${IMUILIB} STATIC
    src/widget.cpp
    src/panel.cpp
    src/flex_panel.cpp
    src/button.cpp
    src/checkbox.cpp
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
