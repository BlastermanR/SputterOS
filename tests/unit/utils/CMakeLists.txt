# Utility Layer Unit Tests
#
# One executable per utility class. All executables carry the CTest label
# "utils" so they can be run in isolation:
#   ctest --test-dir <build> --label-regex utils --output-on-failure
#
# Dependencies:
#   test_utils_pid              -- no mocks (pure math)
#   test_utils_string_builder   -- no mocks (fixed-capacity formatter)
#   test_utils_error_logger     -- no mocks (ring buffer)
#   test_utils_stopwatch        -- no mocks (pure timer math)
#   test_utils_memory_profiler  -- no mocks (stub methods returning 0)
#   test_utils_cli              -- MockStreamReader

function(add_utils_test TARGET_NAME SOURCE_FILE)
    add_executable(${TARGET_NAME} ${SOURCE_FILE})
    target_include_directories(${TARGET_NAME} PRIVATE
        ${SPUTTEROS_TEST_CONFIG_DIR}
        ${SPUTTEROS_TEST_MOCKS_DIR}
    )
    target_link_libraries(${TARGET_NAME} PRIVATE
        SputterOS
        GTest::gtest_main
        GTest::gmock
    )
    gtest_discover_tests(${TARGET_NAME} PROPERTIES LABELS "utils")
endfunction()

add_utils_test(test_utils_pid                test_PIDController.cpp)
add_utils_test(test_utils_string_builder     test_LightweightStringBuilder.cpp)
add_utils_test(test_utils_error_logger       test_ErrorLogger.cpp)
add_utils_test(test_utils_stopwatch          test_NonBlockingStopwatch.cpp)
add_utils_test(test_utils_memory_profiler    test_MemoryProfiler.cpp)
add_utils_test(test_utils_cli                test_CLI.cpp)
add_utils_test(test_utils_telemetry_logger   test_TelemetryLogger.cpp)
add_utils_test(test_utils_op_result          test_OpResult.cpp)
add_utils_test(test_utils_queue_depth_mon    test_QueueDepthMonitor.cpp)
add_utils_test(test_utils_perf_formatter    test_PerformanceFormatter.cpp)
add_utils_test(test_utils_perf_snapshot     test_PerformanceSnapshot.cpp)
add_utils_test(test_utils_platform_assert   test_PlatformAssert.cpp)
