add_library(ui_preview_app STATIC src/app_maker.cpp src/ui_preview.cpp)
target_link_libraries(ui_preview_app PUBLIC imapp_canvas)
target_include_directories(ui_preview_app
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# runtime-TTF builds resolve per-widget font_size against the vendored
# Inter (UI_PREVIEW_FONT overrides at runtime); without the option the
# 5x7 bitmap stays the default and font-size stays ignored
if (USE_TTF_RUNTIME)
    target_compile_definitions(ui_preview_app PRIVATE
        IM_PREVIEW_TTF_FONT="${CMAKE_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf")
endif()