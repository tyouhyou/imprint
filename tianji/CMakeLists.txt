# The tianji client composition layer (Batch FI Part 2). The feed
# library is pure app-side code over imutil/imcore — no framework API
# changes; the ${STORY}-style exe assembly lands with TJ-C3.

add_library(tianji_feed STATIC
    feed/json.cpp
    feed/feed_client.cpp
    feed/mock_server.cpp
    feed/view_builder.cpp
)

target_include_directories(tianji_feed PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tianji_feed PUBLIC imutil imcore)

# the STORY=tianji app library: the root composes the ${STORY} exe
# from shell_backend + ${STORY}_app (A-22, one composition point) --
# same convention as apps/<story>, sourced from tianji/
add_library(tianji_app STATIC
    app/tianji_app.cpp
    app/app_maker.cpp
    app/scene_replay.cpp
)
target_include_directories(tianji_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/app)
target_link_libraries(tianji_app PUBLIC tianji_feed imui imapp_canvas imapp_snapshot)

# the stage placeholder (the g2048 font-blob precedent): a deterministic
# 800x600 PNG packed at build time, the ImageView content behind the
# AnnotateCanvas's letterbox -- the FI-04 ask (tianji has zero scene
# art) stays open; this asset is what makes the stage input-mappable
# today
set(TIANJI_STAGE_HEADER ${CMAKE_CURRENT_BINARY_DIR}/tianji_stage.gen.hpp)
# the gen step needs the bytes_embed host tool (IMPRINT_USE_TOOLS): a
# framework-only subproject configure (A-28, tools off) builds the feed
# library and tests without the app's packed asset. The variable, not
# TARGET bytes_embed: this subdirectory is processed before the tools
# block, so the target does not exist yet at configure time.
if (IMPRINT_USE_TOOLS)
    add_custom_command(
        OUTPUT ${TIANJI_STAGE_HEADER}
        COMMAND bytes_embed ${TIANJI_STAGE_HEADER} tianji_stage
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/stage_placeholder.png
        DEPENDS bytes_embed ${CMAKE_CURRENT_SOURCE_DIR}/assets/stage_placeholder.png
        VERBATIM)
    add_custom_target(tianji_stage_gen DEPENDS ${TIANJI_STAGE_HEADER})
    add_dependencies(tianji_app tianji_stage_gen)
    target_include_directories(tianji_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()

# Linux-only for now: the wire is an AF_UNIX socket and the mock binds
# the Linux abstract namespace (Darwin has none — the macOS maintainer
# box and the macos CI job stay out until a filesystem-socket fallback
# is wanted); the wasm transport seam is TJ-C6. The tests ride the same
# gate (the sockets are not embeddable and win32 CI has no POSIX socket
# API).
if (UNIX AND NOT APPLE)
    add_executable(test_tianji test/test_tianji_feed.cpp)
    target_include_directories(test_tianji PRIVATE ${PROJECT_SOURCE_DIR}/test ${CMAKE_CURRENT_SOURCE_DIR}/app)
    target_link_libraries(test_tianji PRIVATE tianji_app)
    add_test(NAME test_tianji COMMAND test_tianji)
    set_tests_properties(test_tianji PROPERTIES
        ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1")
endif()
