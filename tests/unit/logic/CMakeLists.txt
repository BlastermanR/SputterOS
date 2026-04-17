# Logic Layer Unit Tests
#
# One executable per class under test. All executables carry the CTest label
# "logic" so they can be run in isolation:
#   ctest --test-dir <build> --label-regex logic --output-on-failure
#
# Dependencies injected via mocks:
#   test_logic_command_parser    -- no mocks (pure byte-stream parser)
#   test_logic_interlock_manager -- MockInterlockCondition, MockFaultResponse

# Helper: define a single logic-layer test executable.
function(add_logic_test TARGET_NAME SOURCE_FILE)
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
    gtest_discover_tests(${TARGET_NAME} PROPERTIES LABELS "logic")
endfunction()

add_logic_test(test_logic_command_parser    test_CommandParser.cpp)
add_logic_test(test_logic_interlock_manager test_InterlockManager.cpp)
