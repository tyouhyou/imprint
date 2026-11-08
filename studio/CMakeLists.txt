# The studio client composition layer (P1-10): the five-pane session
# screen client over the studio-bridge child process (tianji repo
# docs/09 §9.1 — UI 与功能分离：本层零逻辑，只做投影与路由). Same shape
# as the tianji/ game client: an app-side bridge library + the
# ${STORY}=studio app library; the exe assembly happens at the root.

# Platform gate: the wire is a spawned child process over pipes —
# fork/exec/fcntl are POSIX. win32 (no fork), NDS and wasm (no child
# processes at all) stay out; unlike the tianji game client there is no
# AF_UNIX dependency, so Darwin is welcome.
if (NOT UNIX OR NDS_PLATFORM OR EMSCRIPTEN)
    return()
endif()

add_library(studio_bridge STATIC
    bridge/bridge_client.cpp
)
target_include_directories(studio_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(studio_bridge PUBLIC tj_json imutil imcore)

add_library(studio_app STATIC
    app/studio_app.cpp
    app/app_maker.cpp
)
target_include_directories(studio_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/app)
target_link_libraries(studio_app PUBLIC studio_bridge imui imapp_canvas imapp_snapshot)

# the bridge protocol suite rides the same battery gate as the in-tree
# test/ dir (root CMakeLists) — the tianji feed precedent
if (IMPRINT_WITH_TESTS)
    add_executable(test_studio test/test_studio_bridge.cpp)
    target_include_directories(test_studio PRIVATE
        ${PROJECT_SOURCE_DIR}/test ${CMAKE_CURRENT_SOURCE_DIR}/app)
    target_link_libraries(test_studio PRIVATE studio_app)
    # the spawn suite drives the stdlib-only mock bridge (no tianji
    # repo needed); the source path rides a compile definition
    target_compile_definitions(test_studio PRIVATE
        STUDIO_MOCK_BRIDGE="${CMAKE_CURRENT_SOURCE_DIR}/bridge/mock_bridge.py")
    add_test(NAME test_studio COMMAND test_studio)
    set_tests_properties(test_studio PROPERTIES
        ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1")
endif()
