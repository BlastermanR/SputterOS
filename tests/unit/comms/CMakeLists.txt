# Comms Protocol Unit Tests
#
# One executable per protocol component. All executables carry the CTest
# label "comms" so they can be run in isolation:
#   ctest --test-dir <build> --label-regex comms --output-on-failure
#
# @author SputterOS Contributors
# @date 4/12/2026

function(add_comms_test TARGET_NAME SOURCE_FILE)
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
    gtest_discover_tests(${TARGET_NAME} PROPERTIES LABELS "comms")
endfunction()

add_comms_test(test_comms_cobs_codec       test_CobsCodec.cpp)
add_comms_test(test_comms_crc16            test_Crc16.cpp)
add_comms_test(test_comms_frame_encoder    test_FrameEncoder.cpp)
add_comms_test(test_comms_frame_decoder    test_FrameDecoder.cpp)
add_comms_test(test_comms_protocol_router  test_ProtocolRouter.cpp)
add_comms_test(test_comms_resp_serializer  test_ResponseSerializer.cpp)
add_comms_test(test_comms_dual_mode_cli    test_DualModeCLI.cpp)
