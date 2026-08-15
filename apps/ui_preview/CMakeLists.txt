add_library(ui_preview_app STATIC src/app_maker.cpp src/ui_preview.cpp)
target_link_libraries(ui_preview_app PUBLIC imapp)
target_include_directories(ui_preview_app
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)