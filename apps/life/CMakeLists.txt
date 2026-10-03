add_library(
    life_app STATIC
    src/app_maker.cpp
    src/life.cpp
)

target_link_libraries(
    life_app
    PUBLIC
    imapp_canvas
)

target_include_directories(
    life_app
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
