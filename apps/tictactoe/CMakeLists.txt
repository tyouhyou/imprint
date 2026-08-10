add_library(
    tictactoe_app STATIC
    src/ai.cpp
    src/app_maker.cpp
    src/board.cpp
    src/game.cpp
    src/tictactoe.cpp
    src/tictactoe_controller.cpp
    src/tictactoe_view.cpp
)

target_link_libraries(
    tictactoe_app
    PUBLIC
    imapp
)

target_include_directories(
    tictactoe_app
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
