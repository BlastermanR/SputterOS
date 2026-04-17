/**
 * @file test_FrameEncoder.cpp
 * @brief Unit tests for FrameEncoder — COBS-framed message encoding.
 *
 * Validates that encoded frames have the correct structure, CRC, and
 * that the FrameDecoder can successfully decode the output.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/12/2026
 */

#include "sputteros/comms/protocol/FrameConstants.h"
#include "sputteros/comms/protocol/FrameDecoder.h"
#include "sputteros/comms/protocol/FrameEncoder.h"
#include "sputteros/comms/protocol/MessageType.h"
#include <cstring>
#include <gtest/gtest.h>

using namespace SputterOS;

static constexpr std::size_t kTestPayload = 256;
using TestEncoder                         = FrameEncoder<kTestPayload>;
using TestDecoder                         = FrameDecoder<kTestPayload>;

// ===========================================================================
// Basic encoding
// ===========================================================================

TEST(FrameEncoderTest, EncodeEmptyPayload_ReturnsNonZero)
{
    TestEncoder enc;
    uint8_t     wire[TestEncoder::kMaxWireSize];
    std::size_t n = enc.encode(MessageType::HEARTBEAT, 0, nullptr, 0, wire, sizeof(wire));
    ASSERT_GT(n, 0u);

    // Last byte must be the 0x00 delimiter.
    EXPECT_EQ(wire[n - 1], kFrameDelimiter);
}

TEST(FrameEncoderTest, EncodeWithPayload_ReturnsNonZero)
{
    TestEncoder   enc;
    const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t       wire[TestEncoder::kMaxWireSize];
    std::size_t   n = enc.encode(MessageType::COMMAND, 42, payload, 4, wire, sizeof(wire));
    ASSERT_GT(n, 0u);
    EXPECT_EQ(wire[n - 1], kFrameDelimiter);
}

// ===========================================================================
// Roundtrip: encode → decode
// ===========================================================================

TEST(FrameEncoderTest, Roundtrip_EmptyPayload)
{
    TestEncoder enc;
    TestDecoder dec;

    uint8_t     wire[TestEncoder::kMaxWireSize];
    std::size_t n = enc.encode(MessageType::ACK, 7, nullptr, 0, wire, sizeof(wire));
    ASSERT_GT(n, 0u);

    DecoderResult result = DecoderResult::INCOMPLETE;
    for (std::size_t i = 0; i < n; ++i)
    {
        result = dec.feedByte(wire[i]);
    }
    ASSERT_EQ(result, DecoderResult::FRAME_READY);
    EXPECT_EQ(dec.getMessageType(), MessageType::ACK);
    EXPECT_EQ(dec.getSeqNum(), 7u);
    EXPECT_EQ(dec.getPayloadLen(), 0u);
}

TEST(FrameEncoderTest, Roundtrip_CommandPayload)
{
    TestEncoder enc;
    TestDecoder dec;

    // COMMAND payload: [CmdID:1][device:1][value:4LE]
    uint8_t payload[6];
    payload[0] = 0x01; // CmdID
    payload[1] = 0x00; // device
    float val  = 50.0f;
    std::memcpy(&payload[2], &val, sizeof(val)); // value LE

    uint8_t     wire[TestEncoder::kMaxWireSize];
    std::size_t n = enc.encode(MessageType::COMMAND, 1, payload, 6, wire, sizeof(wire));
    ASSERT_GT(n, 0u);

    DecoderResult result = DecoderResult::INCOMPLETE;
    for (std::size_t i = 0; i < n; ++i)
    {
        result = dec.feedByte(wire[i]);
    }
    ASSERT_EQ(result, DecoderResult::FRAME_READY);
    EXPECT_EQ(dec.getMessageType(), MessageType::COMMAND);
    EXPECT_EQ(dec.getSeqNum(), 1u);
    ASSERT_EQ(dec.getPayloadLen(), 6u);
    EXPECT_EQ(dec.getPayload()[0], 0x01);
    EXPECT_EQ(dec.getPayload()[1], 0x00);

    float decoded;
    std::memcpy(&decoded, &dec.getPayload()[2], sizeof(float));
    EXPECT_FLOAT_EQ(decoded, 50.0f);
}

TEST(FrameEncoderTest, Roundtrip_MaxPayload)
{
    TestEncoder enc;
    TestDecoder dec;

    uint8_t payload[kTestPayload];
    for (std::size_t i = 0; i < kTestPayload; ++i)
    {
        payload[i] = static_cast<uint8_t>(i & 0xFF);
    }

    uint8_t     wire[TestEncoder::kMaxWireSize];
    std::size_t n = enc.encode(MessageType::DATA, 99, payload, kTestPayload, wire, sizeof(wire));
    ASSERT_GT(n, 0u);

    DecoderResult result = DecoderResult::INCOMPLETE;
    for (std::size_t i = 0; i < n; ++i)
    {
        result = dec.feedByte(wire[i]);
    }
    ASSERT_EQ(result, DecoderResult::FRAME_READY);
    EXPECT_EQ(dec.getMessageType(), MessageType::DATA);
    EXPECT_EQ(dec.getSeqNum(), 99u);
    ASSERT_EQ(dec.getPayloadLen(), kTestPayload);
    EXPECT_EQ(std::memcmp(dec.getPayload(), payload, kTestPayload), 0);
}

TEST(FrameEncoderTest, Roundtrip_AllMessageTypes)
{
    TestEncoder enc;

    const MessageType types[] = {
        MessageType::COMMAND,      MessageType::HANDSHAKE_REQ, MessageType::METRICS_REQ,    MessageType::EXIT_HANDSHAKE,
        MessageType::ACK,          MessageType::NACK,          MessageType::HANDSHAKE_RESP, MessageType::TELEMETRY,
        MessageType::METRICS_RESP, MessageType::LOG,           MessageType::DATA,           MessageType::PERF_DATA,
        MessageType::HEARTBEAT,
    };

    const uint8_t payload[] = {0xAA, 0xBB};
    uint8_t       wire[TestEncoder::kMaxWireSize];

    for (auto t : types)
    {
        TestDecoder dec;
        std::size_t n = enc.encode(t, 0, payload, 2, wire, sizeof(wire));
        ASSERT_GT(n, 0u) << "Encode failed for type " << static_cast<int>(t);

        DecoderResult result = DecoderResult::INCOMPLETE;
        for (std::size_t i = 0; i < n; ++i)
        {
            result = dec.feedByte(wire[i]);
        }
        ASSERT_EQ(result, DecoderResult::FRAME_READY) << "Decode failed for type " << static_cast<int>(t);
        EXPECT_EQ(dec.getMessageType(), t);
    }
}

// ===========================================================================
// Error cases
// ===========================================================================

TEST(FrameEncoderTest, Encode_PayloadTooLarge_ReturnsZero)
{
    FrameEncoder<8> enc;
    uint8_t         payload[16];
    uint8_t         wire[512];
    EXPECT_EQ(enc.encode(MessageType::DATA, 0, payload, 16, wire, sizeof(wire)), 0u);
}

TEST(FrameEncoderTest, Encode_OutputBufferTooSmall_ReturnsZero)
{
    TestEncoder   enc;
    const uint8_t payload[] = {0x01};
    uint8_t       wire[2]; // Way too small
    EXPECT_EQ(enc.encode(MessageType::ACK, 0, payload, 1, wire, sizeof(wire)), 0u);
}

// ===========================================================================
// Wire format verification
// ===========================================================================

TEST(FrameEncoderTest, WireEndsWithDelimiter)
{
    TestEncoder enc;
    uint8_t     wire[TestEncoder::kMaxWireSize];
    std::size_t n = enc.encode(MessageType::HEARTBEAT, 0, nullptr, 0, wire, sizeof(wire));
    ASSERT_GT(n, 0u);
    EXPECT_EQ(wire[n - 1], 0x00);
}

TEST(FrameEncoderTest, NoBareZerosBeforeDelimiter)
{
    TestEncoder   enc;
    const uint8_t payload[] = {0x00, 0x00, 0x00}; // Payload with zeros
    uint8_t       wire[TestEncoder::kMaxWireSize];
    std::size_t   n = enc.encode(MessageType::DATA, 0, payload, 3, wire, sizeof(wire));
    ASSERT_GT(n, 0u);

    // All bytes except the final delimiter must be non-zero.
    for (std::size_t i = 0; i < n - 1; ++i)
    {
        EXPECT_NE(wire[i], 0x00) << "Unexpected zero at wire offset " << i;
    }
}
