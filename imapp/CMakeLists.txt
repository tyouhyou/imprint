add_library(
    ${IMAPPLIB} INTERFACE
    include/app_maker.hpp
    include/canvas_window.hpp
    include/iapp.hpp
    include/igui.hpp
    include/iwindow.hpp
    include/imapp.hpp
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
    ${IMUILIB}
    ${IMINPUTLIB}
)
