# System Test Suite for SputterOS
#
# GoogleTest-based integration tests that validate critical kernel
# interactions using real SputterOS components (not mocks).  Each test
# file focuses on a specific subsystem interaction.
#
# This file is included via add_subdirectory() from the parent
# tests/CMakeLists.txt which provides: GoogleTest, SputterOS library,
# Threads, and coverage flags.
#
# @author Ryan Massie (rmassie)
# @date 4/9/2026

# ── Shared include paths ─────────────────────────────────────────────────────
set(SYSTEST_CONFIG_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/config)
set(SYSTEST_HELPERS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/helpers)

# ── Helper function ──────────────────────────────────────────────────────────
function(add_system_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})

    target_include_directories(${TEST_NAME} PRIVATE
        ${SYSTEST_CONFIG_DIR}
        ${SYSTEST_HELPERS_DIR}
    )

    target_link_libraries(${TEST_NAME} PRIVATE
        SputterOS
        GTest::gtest_main
        Threads::Threads
    )

    if(MSVC)
        target_compile_options(${TEST_NAME} PRIVATE /W4)
    else()
        target_compile_options(${TEST_NAME} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    gtest_discover_tests(${TEST_NAME}
        PROPERTIES LABELS "system"
    )
endfunction()

# ── Test executables ─────────────────────────────────────────────────────────
add_system_test(test_SystemLifecycle        test_SystemLifecycle.cpp)
add_system_test(test_TimerRollover          test_TimerRollover.cpp)
add_system_test(test_CommandQueueFlow       test_CommandQueueFlow.cpp)
add_system_test(test_DiagnosticsMonitoring  test_DiagnosticsMonitoring.cpp)
add_system_test(test_MultiCoreSync          test_MultiCoreSync.cpp)
add_system_test(test_SafetyAbortPipeline    test_SafetyAbortPipeline.cpp)
add_system_test(test_CommandDeliveryPipeline test_CommandDeliveryPipeline.cpp)
add_system_test(test_DualCorePipeline       test_DualCorePipeline.cpp)
add_system_test(test_InterlockPipeline      test_InterlockPipeline.cpp)
add_system_test(test_ErrorLoggingPipeline   test_ErrorLoggingPipeline.cpp)
add_system_test(test_StateMachinePipeline   test_StateMachinePipeline.cpp)
add_system_test(test_SchedulingStress       test_SchedulingStress.cpp)
