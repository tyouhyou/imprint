add_library(hello_app STATIC src/app_maker.cpp)
target_link_libraries(hello_app PUBLIC imapp_canvas)
target_include_directories(hello_app
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
