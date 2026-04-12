# HeartBeat example project — OS-native build
#
# Produces one executable target:
#   heartbeat_osNative — runs all three critical SputterOS tasks plus
#                        the custom PulseTask in a single-core main loop.
#
# Registered as a CTest test labeled "system_tests".  Exit code 0 = PASSED.
#
# To add a Pico variant later:
#   add_example_variant(heartbeat_pico src/main_pico.cpp
#       config/HeartbeatConfig_pico.h)
#
# @author Ryan Massie (rmassie)
# @date 4/8/2026

add_executable(heartbeat_osNative
    src/main_osNative.cpp
)

target_compile_features(heartbeat_osNative PRIVATE cxx_std_17)

# Include paths — searched in declaration order so project headers take
# priority over any identically named SputterOS internal headers.
target_include_directories(heartbeat_osNative PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/config      # HeartbeatConfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/hal_impl    # HeartbeatHAL.h
    ${CMAKE_CURRENT_SOURCE_DIR}/osal_impl   # HeartbeatOSAL.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include    # HeartbeatStateMachine.h, PulseTask.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/hal  # TcpStreamServer.h
)

target_link_libraries(heartbeat_osNative PRIVATE
    SputterOS        # SputterOS header-only + compiled utils
    Threads::Threads # std::this_thread::sleep_for
)

if(WIN32)
    target_link_libraries(heartbeat_osNative PRIVATE ws2_32)
endif()

# Strict warnings — match the quality bar used across other test targets.
if(MSVC)
    target_compile_options(heartbeat_osNative PRIVATE /W4)
else()
    target_compile_options(heartbeat_osNative PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

# ── CTest registration ────────────────────────────────────────────────────────
add_test(
    NAME    heartbeat_osNative
    COMMAND heartbeat_osNative
)
set_tests_properties(heartbeat_osNative PROPERTIES
    LABEL   "example_projects"
    TIMEOUT 30      # seconds; 5 pulses × 500 ms + overhead well within limit
)
