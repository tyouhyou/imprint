add_library(
    g2048_app STATIC
    src/app_maker.cpp
    src/g2048.cpp
)

target_link_libraries(
    g2048_app
    PUBLIC
    imapp_canvas
)

target_include_directories(
    g2048_app
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
