set(STORY tictactoe CACHE STRING "Demo application to build")
set_property(CACHE STORY PROPERTY STRINGS tictactoe ui_preview)

# app libraries are always built on desktop (tests link them); STORY only
# selects which one the shell executable links. The previewer is a
# desktop tool and never builds for embedded targets (contract 5.3).
add_subdirectory(tictactoe)
if (NOT NDS_PLATFORM)
    add_subdirectory(ui_preview)
endif()
if (NOT STORY STREQUAL "tictactoe" AND NOT STORY STREQUAL "ui_preview")
    message(FATAL_ERROR "Unknown STORY: ${STORY}")
endif()