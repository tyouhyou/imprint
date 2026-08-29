# A-22 split: `imapp` is the app interface — IApp/IWindow/IGui and the
# make_app() entry — with no widget dependency, so a graphics-only app
# links nothing below imcore/imevent/iminput. CanvasWindow (the default
# IWindow over the widget tree) is an optional add-on: `imapp_canvas`
# pulls imui. The imapp.hpp umbrella includes canvas_window.hpp and
# therefore needs imapp_canvas on the link line.

add_library(
    ${IMAPPLIB} INTERFACE
    include/app_maker.hpp
    include/iapp.hpp
    include/igui.hpp
    include/iwindow.hpp
)

target_include_directories(
    ${IMAPPLIB}
    INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
    ${IMAPPLIB}
    INTERFACE
    ${IMCORELIB}
    ${IMEVENTLIB}
    ${IMINPUTLIB}
)

add_library(
    imapp_canvas INTERFACE
    include/canvas_window.hpp
    include/imapp.hpp
)

target_include_directories(
    imapp_canvas
    INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
    imapp_canvas
    INTERFACE
    ${IMAPPLIB}
    ${IMUILIB}
)
