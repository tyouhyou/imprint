set(STORY tictactoe CACHE STRING "Demo application to build")
set_property(CACHE STORY PROPERTY STRINGS tictactoe ui_preview showcase hello showcase_html g2048 mines life seedmap)

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
# The gh-pages demo-portal apps (2026-10-03): desktop + wasm only. They
# gained a build-time bytes_embed step for the web tier's runtime-TTF text
# (2026-10-07), and bytes_embed is a host tool the top level does not
# build under the NDS toolchain. They have no NDS ROM story, so they stay
# out of cross builds entirely — the same rule ui_preview follows below.
if (NOT NDS_PLATFORM)
    add_subdirectory(g2048)
    add_subdirectory(mines)
    add_subdirectory(life)
    add_subdirectory(seedmap)
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
    AND NOT STORY STREQUAL "showcase_html" AND NOT STORY STREQUAL "g2048"
    AND NOT STORY STREQUAL "mines" AND NOT STORY STREQUAL "life"
    AND NOT STORY STREQUAL "seedmap")
    message(FATAL_ERROR "Unknown STORY: ${STORY}")
endif()