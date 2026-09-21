set(STORY tictactoe CACHE STRING "Demo application to build")
set_property(CACHE STORY PROPERTY STRINGS tictactoe ui_preview showcase hello showcase_html)

# app libraries are always built on desktop (tests link them); STORY only
# selects which one the shell executable links. The previewer is a
# desktop tool and never builds for embedded targets (contract 5.3).
# The showcase builds for NDS only when selected: its build-time ui_embed
# step then needs a host-built tool (UI_EMBED_EXECUTABLE), and the
# default NDS CI job (STORY=tictactoe) must stay untouched.
# hello is the getting-started app (docs/getting-started.md); it has no
# build-time tooling and builds for every target.
add_subdirectory(tictactoe)
add_subdirectory(hello)
if (NOT NDS_PLATFORM)
    add_subdirectory(ui_preview)
endif()
if (NOT NDS_PLATFORM OR STORY STREQUAL "showcase")
    add_subdirectory(showcase)
endif()
# showcase_html follows the showcase rule: the desktop library always
# builds, the NDS ROM needs a host-built html_embed handed in
if (NOT NDS_PLATFORM OR STORY STREQUAL "showcase_html")
    add_subdirectory(showcase_html)
endif()
if (NOT STORY STREQUAL "tictactoe" AND NOT STORY STREQUAL "ui_preview"
    AND NOT STORY STREQUAL "showcase" AND NOT STORY STREQUAL "hello"
    AND NOT STORY STREQUAL "showcase_html")
    message(FATAL_ERROR "Unknown STORY: ${STORY}")
endif()