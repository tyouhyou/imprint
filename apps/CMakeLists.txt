set(STORY tictactoe CACHE STRING "Demo application to build")
set_property(CACHE STORY PROPERTY STRINGS tictactoe ui_preview)

# app libraries are always built (tests link tictactoe_app); STORY only
# selects which one the shell executable links
add_subdirectory(tictactoe)
if (STORY STREQUAL "ui_preview")
    add_subdirectory(ui_preview)
elseif (NOT STORY STREQUAL "tictactoe")
    message(FATAL_ERROR "Unknown STORY: ${STORY}")
endif()