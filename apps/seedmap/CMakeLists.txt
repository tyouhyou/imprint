add_library(
    seedmap_app STATIC
    src/app_maker.cpp
    src/seedmap.cpp
)

target_link_libraries(
    seedmap_app
    PUBLIC
    imapp_canvas
)

target_include_directories(
    seedmap_app
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
