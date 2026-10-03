add_library(
    mines_app STATIC
    src/app_maker.cpp
    src/mines.cpp
)

target_link_libraries(
    mines_app
    PUBLIC
    imapp_canvas
)

target_include_directories(
    mines_app
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
