add_library(showcase_app STATIC src/app_maker.cpp src/showcase.cpp)
target_link_libraries(showcase_app PUBLIC imapp_canvas)
target_include_directories(showcase_app
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# design documents packed at build time (contract: build-time validation
# with the same parser; NDS has no filesystem, so the embedded bytes are
# the shipping form on every target)
set(SHOWCASE_UI
    ${CMAKE_CURRENT_SOURCE_DIR}/hero.ui
    ${CMAKE_CURRENT_SOURCE_DIR}/gallery.ui)
set(SHOWCASE_UI_HEADER ${CMAKE_CURRENT_BINARY_DIR}/showcase_ui.gen.hpp)
if (NDS_PLATFORM)
    # the ui_embed host tool does not build under the ARM toolchain; the
    # cross build must be handed a host-built executable instead of
    # skipping validation silently
    set(UI_EMBED_EXECUTABLE "" CACHE FILEPATH
        "Host-built ui_embed executable for embedded showcase builds")
    if (UI_EMBED_EXECUTABLE STREQUAL "")
        message(FATAL_ERROR
            "showcase on NDS requires -DUI_EMBED_EXECUTABLE=<path to a host-built ui_embed>")
    endif()
    add_custom_command(
        OUTPUT ${SHOWCASE_UI_HEADER}
        COMMAND ${UI_EMBED_EXECUTABLE} ${SHOWCASE_UI_HEADER} ${SHOWCASE_UI}
        DEPENDS ${SHOWCASE_UI}
        VERBATIM)
else()
    add_custom_command(
        OUTPUT ${SHOWCASE_UI_HEADER}
        COMMAND ui_embed ${SHOWCASE_UI_HEADER} ${SHOWCASE_UI}
        DEPENDS ui_embed ${SHOWCASE_UI}
        VERBATIM)
endif()
add_custom_target(showcase_ui_gen DEPENDS ${SHOWCASE_UI_HEADER})
add_dependencies(showcase_app showcase_ui_gen)
target_include_directories(showcase_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# procedural demo assets (V-2): asset_gen emits RGBA8 arrays at build
# time. Host tool like ui_embed — the cross build is handed a
# host-built executable instead of generating on the ARM side
set(SHOWCASE_ASSETS_HEADER ${CMAKE_CURRENT_BINARY_DIR}/showcase_assets.gen.hpp)
if (NDS_PLATFORM)
    set(ASSET_GEN_EXECUTABLE "" CACHE FILEPATH
        "Host-built asset_gen executable for embedded showcase builds")
    if (ASSET_GEN_EXECUTABLE STREQUAL "")
        message(FATAL_ERROR
            "showcase on NDS requires -DASSET_GEN_EXECUTABLE=<path to a host-built asset_gen>")
    endif()
    add_custom_command(
        OUTPUT ${SHOWCASE_ASSETS_HEADER}
        COMMAND ${ASSET_GEN_EXECUTABLE} ${SHOWCASE_ASSETS_HEADER}
        DEPENDS ${ASSET_GEN_EXECUTABLE}
        VERBATIM)
else()
    add_custom_command(
        OUTPUT ${SHOWCASE_ASSETS_HEADER}
        COMMAND asset_gen ${SHOWCASE_ASSETS_HEADER}
        DEPENDS asset_gen
        VERBATIM)
endif()
add_custom_target(showcase_assets_gen DEPENDS ${SHOWCASE_ASSETS_HEADER})
add_dependencies(showcase_app showcase_assets_gen)
target_include_directories(showcase_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

if (NOT NDS_PLATFORM)
    # S4 frame recorder: drives the app through the public API and dumps
    # the frames as an animated GIF (the README showcase asset); the
    # encoder is tool-local, not part of the shipping app library
    add_executable(showcase_gif src/gif_record.cpp src/gif_encoder.cpp)
    target_link_libraries(showcase_gif PRIVATE showcase_app)
endif()
