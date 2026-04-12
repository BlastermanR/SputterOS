# PingPong example project — OS-native dual-core build
#
# Produces one executable target:
#   pingpong_osNative — runs the full SputterOS dual-core kernel with two
#                       communicating user tasks (PingTask on Core 0,
#                       PongTask on Core 1) in separate std::threads.
#
# The two tasks exchange a shared atomic counter guarded by a turn-token flag.
# Memory ordering (release/acquire on the turn flag) is the only inter-core
# synchronization mechanism — no mutexes, no condition variables.
#
# Registered as a CTest test labeled "example_projects".  Exit code 0 = PASSED.
#
# @author Ryan Massie (rmassie)
# @date 4/10/2026

add_executable(pingpong_osNative
    src/main_osNative.cpp
)

target_compile_features(pingpong_osNative PRIVATE cxx_std_17)

# Include paths — searched in declaration order so project headers take
# priority over any identically named SputterOS internal headers.
target_include_directories(pingpong_osNative PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/config      # PingPongConfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/hal_impl    # PingPongHAL.h
    ${CMAKE_CURRENT_SOURCE_DIR}/osal_impl   # PingPongOSAL.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include     # PingPongStateMachine.h, SharedCounter.h,
                                            # PingTask.h, PongTask.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/hal  # TcpStreamServer.h
)

target_link_libraries(pingpong_osNative PRIVATE
    SputterOS        # SputterOS header-only + compiled utils
    Threads::Threads # std::thread, std::this_thread::sleep_for
)

if(WIN32)
    target_link_libraries(pingpong_osNative PRIVATE ws2_32)
endif()

# Strict warnings — match the quality bar used across other test targets.
if(MSVC)
    target_compile_options(pingpong_osNative PRIVATE /W4)
else()
    target_compile_options(pingpong_osNative PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

# ── CTest registration ────────────────────────────────────────────────────────
add_test(
    NAME    pingpong_osNative
    COMMAND pingpong_osNative
)
set_tests_properties(pingpong_osNative PROPERTIES
    LABEL   "example_projects"
    TIMEOUT 15      # seconds; 5 rounds × 300 ms + overhead well within limit
)
