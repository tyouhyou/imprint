set(STORY tictactoe CACHE STRING "Demo application to build")
set_property(CACHE STORY PROPERTY STRINGS tictactoe ui_preview showcase hello showcase_html g2048 mines life seedmap seq303 drift sandbox firefly windloom rotafarm comet crystal moonpull fogkeeper hoursmith)

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
    add_subdirectory(seq303)
    add_subdirectory(drift)
    add_subdirectory(sandbox)
    add_subdirectory(firefly)
    add_subdirectory(windloom)
    add_subdirectory(rotafarm)
    add_subdirectory(comet)
    add_subdirectory(crystal)
    add_subdirectory(ui_preview)
endif()
# the 3D stories (2026-10-26) consume imraster, which only configures in
# the rgba32/le pixel model (ARCHITECTURE 4.12) -- they exist exactly
# where the raster module does, and never in the default configs
if (IMPRINT_WITH_IMRASTER)
    add_subdirectory(moonpull)
    add_subdirectory(fogkeeper)
    add_subdirectory(hoursmith)
elseif (STORY STREQUAL "moonpull" OR STORY STREQUAL "fogkeeper" OR STORY STREQUAL "hoursmith")
    message(FATAL_ERROR
        "STORY=${STORY} is a 3D story and needs the raster module: "
        "configure with -DIMPRINT_WITH_IMRASTER=ON -DRGB_MODEL=rgba32 -DENDIAN=le")
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
    AND NOT STORY STREQUAL "seedmap" AND NOT STORY STREQUAL "seq303"
    AND NOT STORY STREQUAL "drift" AND NOT STORY STREQUAL "sandbox" AND NOT STORY STREQUAL "firefly" AND NOT STORY STREQUAL "windloom" AND NOT STORY STREQUAL "rotafarm" AND NOT STORY STREQUAL "comet" AND NOT STORY STREQUAL "crystal" AND NOT STORY STREQUAL "moonpull" AND NOT STORY STREQUAL "fogkeeper" AND NOT STORY STREQUAL "hoursmith")
    message(FATAL_ERROR "Unknown STORY: ${STORY}")
endif()