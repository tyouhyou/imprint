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

# POSIX-only for now: the wire is an AF_UNIX socket; the wasm transport
# seam is TJ-C6. The tests ride the same gate (the sockets are not
# embeddable and win32 CI has no POSIX socket API).
if (UNIX)
    add_executable(test_tianji test/test_tianji_feed.cpp)
    target_include_directories(test_tianji PRIVATE ${PROJECT_SOURCE_DIR}/test)
    target_link_libraries(test_tianji PRIVATE tianji_feed)
    add_test(NAME test_tianji COMMAND test_tianji)
    set_tests_properties(test_tianji PROPERTIES
        ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1")
endif()
