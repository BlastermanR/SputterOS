# Task Layer Unit Tests
#
# One executable per task class. All executables carry the CTest label
# "tasks" so they can be run in isolation:
#   ctest --test-dir <build> --label-regex tasks --output-on-failure
#
# Dependencies injected via mocks:
#   test_tasks_control_task    -- MockMessageQueue, MockArcDetector,
#                                  MockUserApplication (IUserApplication),
#                                  real InterlockManager (with MockVacuumGauge, MockRelay)
#   test_tasks_comms_task      -- MockStreamReader, MockMessageQueue
#   test_tasks_diagnostics_task -- real ErrorLogger, real TaskTimer,
#                                  real MemoryProfiler, WatchdogKickFn pointer

function(add_tasks_test TARGET_NAME SOURCE_FILE)
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
    gtest_discover_tests(${TARGET_NAME} PROPERTIES LABELS "tasks")
endfunction()

add_tasks_test(test_tasks_control_task     test_ControlTask.cpp)
add_tasks_test(test_tasks_comms_task       test_CommsTask.cpp)
add_tasks_test(test_tasks_diagnostics_task test_DiagnosticsTask.cpp)
add_tasks_test(test_tasks_system_builder   test_SystemBuilder.cpp)
add_tasks_test(test_tasks_task_timer       test_TaskTimer.cpp)
add_tasks_test(test_tasks_deadline_tracker test_DeadlineTracker.cpp)
add_tasks_test(test_tasks_kernel_state     test_KernelState.cpp)
add_tasks_test(test_tasks_multirate_flow   test_MultiRateDataFlow.cpp)
add_tasks_test(test_tasks_io_pending       test_IoPendingPattern.cpp)
add_tasks_test(test_tasks_core_util        test_CoreUtilizationTracker.cpp)
add_tasks_test(test_tasks_sched_health     test_SchedulerHealthMetrics.cpp)
add_tasks_test(test_tasks_system_run       test_SystemRun.cpp)
add_tasks_test(test_tasks_bg_dispatcher    test_BackgroundDispatcher.cpp)
