# SensorPoll example project — OS-native build
#
# Produces one executable target:
#   sensorpoll_osNative — runs a single-core kernel demonstrating the
#                         IO_PENDING non-blocking IO pattern with a
#                         simulated ADC and background data logging.
#
# Demonstrates:
#   - IO_PENDING task state (yield-and-poll pattern)
#   - Fault detection on hung IO (timeout after N retries)
#   - Background gap-time data aggregation
#   - Simulated hardware device (SimulatedADC with conversion delay)
#
# Registered as a CTest test labeled "example_projects".  Exit code 0 = PASSED.
#
# @author Ryan Massie (rmassie)
# @date 4/11/2026

add_executable(sensorpoll_osNative
    src/main_osNative.cpp
)

target_compile_features(sensorpoll_osNative PRIVATE cxx_std_17)

target_include_directories(sensorpoll_osNative PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/config      # SensorPollConfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/hal_impl    # SensorPollHAL.h
    ${CMAKE_CURRENT_SOURCE_DIR}/osal_impl   # SensorPollOSAL.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include     # SensorPollStateMachine.h, tasks
    ${CMAKE_CURRENT_SOURCE_DIR}/..           # common/hal/, common/osal/
)

target_link_libraries(sensorpoll_osNative PRIVATE
    SputterOS        # SputterOS header-only + compiled utils
    Threads::Threads # std::this_thread::sleep_for
)

if(WIN32)
    target_link_libraries(sensorpoll_osNative PRIVATE ws2_32)
endif()

if(MSVC)
    target_compile_options(sensorpoll_osNative PRIVATE /W4)
else()
    target_compile_options(sensorpoll_osNative PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

# ── CTest registration ────────────────────────────────────────────────────────
add_test(
    NAME    sensorpoll_osNative
    COMMAND sensorpoll_osNative
)
set_tests_properties(sensorpoll_osNative PROPERTIES
    LABEL   "example_projects"
    TIMEOUT 15
)
