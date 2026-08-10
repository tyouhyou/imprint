set(STORY tictactoe CACHE STRING "Demo application to build")
set_property(CACHE STORY PROPERTY STRINGS tictactoe)

if (STORY STREQUAL "tictactoe")
    add_subdirectory(tictactoe)
else()
    message(FATAL_ERROR "Unknown STORY: ${STORY}")
endif()
