# OSAL Layer Unit Tests
#
# Tests for OS abstraction interfaces and utilities.
# MultiCoreSync lifecycle barriers, ICoreErrorHandler callbacks.
# ITask device dependency registration and validation.

# Helper: define a single OSAL-layer test executable.
function(add_osal_test TARGET_NAME SOURCE_FILE)
    add_executable(${TARGET_NAME} ${SOURCE_FILE})
    target_include_directories(${TARGET_NAME} PRIVATE
        ${SPUTTEROS_TEST_CONFIG_DIR}
        ${SPUTTEROS_TEST_MOCKS_DIR}
    )
    target_link_libraries(${TARGET_NAME} PRIVATE
        SputterOS
        GTest::gtest_main
    )
    gtest_discover_tests(${TARGET_NAME} PROPERTIES LABELS "osal")
endfunction()

add_executable(test_osal
    test_MultiCoreSync.cpp
    test_ITask.cpp
)

target_link_libraries(test_osal
    PRIVATE
        SputterOS
        GTest::gtest_main
)

# Label for ctest filtering
set_target_properties(test_osal PROPERTIES
    LABELS "osal"
)

gtest_discover_tests(test_osal)

add_osal_test(test_osal_lock_free_queue  test_LockFreeQueue.cpp)
add_osal_test(test_osal_sputter_time     test_SputterTime.cpp)
add_osal_test(test_osal_watchdog_sync    test_WatchdogSync.cpp)
add_osal_test(test_osal_scheduled_task   test_IScheduledTask.cpp)
add_osal_test(test_osal_background_task       test_IBackgroundTask.cpp)
add_osal_test(test_osal_atomic_double_buffer  test_AtomicDoubleBuffer.cpp)
add_osal_test(test_osal_crunch_task      test_ICrunchTask.cpp)
add_osal_test(test_osal_config_traits    test_ConfigTraits.cpp)