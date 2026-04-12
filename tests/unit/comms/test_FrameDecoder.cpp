/**
 * @file test_FrameDecoder.cpp
 * @brief Unit tests for FrameDecoder — COBS frame decoding with CRC validation.
 *
 * Validates frame accumulation, CRC checking, partial frames, re-sync
 * after garbage, and correct header/payload extraction.
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
using TestEncoder = FrameEncoder<kTestPayload>;
using TestDecoder = FrameDecoder<kTestPayload>;

// ===========================================================================
// Helper: encode a frame then feed it to a decoder
// ===========================================================================

static DecoderResult feedEncodedFrame(TestDecoder &dec, MessageType type, uint8_t seq,
                                      const uint8_t *payload, std::size_t payLen)
{
    TestEncoder enc;
    uint8_t wire[TestEncoder::kMaxWireSize];
    std::size_t n = enc.encode(type, seq, payload, payLen, wire, sizeof(wire));
    if (n == 0)
    {
        return DecoderResult::ERROR;
    }

    DecoderResult result = DecoderResult::INCOMPLETE;
    for (std::size_t i = 0; i < n; ++i)
    {
        result = dec.feedByte(wire[i]);
    }
    return result;
}

// ===========================================================================
// Basic decoding
// ===========================================================================

TEST(FrameDecoderTest, DecodeValidFrame_EmptyPayload)
{
    TestDecoder dec;
    auto result = feedEncodedFrame(dec, MessageType::HEARTBEAT, 5, nullptr, 0);
    ASSERT_EQ(result, DecoderResult::FRAME_READY);
    EXPECT_EQ(dec.getMessageType(), MessageType::HEARTBEAT);
    EXPECT_EQ(dec.getSeqNum(), 5u);
    EXPECT_EQ(dec.getPayloadLen(), 0u);
}

TEST(FrameDecoderTest, DecodeValidFrame_WithPayload)
{
    TestDecoder dec;
    const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    auto result = feedEncodedFrame(dec, MessageType::DATA, 42, payload, 4);
    ASSERT_EQ(result, DecoderResult::FRAME_READY);
    EXPECT_EQ(dec.getMessageType(), MessageType::DATA);
    EXPECT_EQ(dec.getSeqNum(), 42u);
    ASSERT_EQ(dec.getPayloadLen(), 4u);
    EXPECT_EQ(std::memcmp(dec.getPayload(), payload, 4), 0);
}

// ===========================================================================
// Sequential frames
// ===========================================================================

TEST(FrameDecoderTest, TwoConsecutiveFrames)
{
    TestDecoder dec;

    const uint8_t p1[] = {0x01};
    auto r1 = feedEncodedFrame(dec, MessageType::ACK, 1, p1, 1);
    ASSERT_EQ(r1, DecoderResult::FRAME_READY);
    EXPECT_EQ(dec.getSeqNum(), 1u);

    const uint8_t p2[] = {0x02};
    auto r2 = feedEncodedFrame(dec, MessageType::NACK, 2, p2, 1);
    ASSERT_EQ(r2, DecoderResult::FRAME_READY);
    EXPECT_EQ(dec.getSeqNum(), 2u);
}

// ===========================================================================
// CRC corruption detection
// ===========================================================================

TEST(FrameDecoderTest, CorruptCRC_ReturnsError)
{
    TestEncoder enc;
    TestDecoder dec;

    uint8_t wire[TestEncoder::kMaxWireSize];
    std::size_t n = enc.encode(MessageType::COMMAND, 0, nullptr, 0, wire, sizeof(wire));
    ASSERT_GT(n, 2u); // Need at least delimiter + some data

    // Corrupt a byte in the middle of the encoded data (before delimiter).
    wire[1] ^= 0xFF;

    DecoderResult result = DecoderResult::INCOMPLETE;
    for (std::size_t i = 0; i < n; ++i)
    {
        result = dec.feedByte(wire[i]);
    }
    EXPECT_EQ(result, DecoderResult::ERROR);
}

// ===========================================================================
// Consecutive delimiters (empty frames)
// ===========================================================================

TEST(FrameDecoderTest, ConsecutiveDelimiters_Ignored)
{
    TestDecoder dec;

    // Feed multiple 0x00 — all should return INCOMPLETE.
    for (int i = 0; i < 5; ++i)
    {
        EXPECT_EQ(dec.feedByte(0x00), DecoderResult::INCOMPLETE);
    }

    // Now feed a valid frame — should decode fine.
    auto result = feedEncodedFrame(dec, MessageType::HEARTBEAT, 0, nullptr, 0);
    EXPECT_EQ(result, DecoderResult::FRAME_READY);
}

// ===========================================================================
// Garbage followed by valid frame (re-sync)
// ===========================================================================

TEST(FrameDecoderTest, GarbageThenValidFrame_ResyncsOnDelimiter)
{
    TestDecoder dec;

    // Feed garbage bytes.
    for (int i = 1; i < 20; ++i) // Non-zero to avoid triggering delimiter
    {
        dec.feedByte(static_cast<uint8_t>(i));
    }

    // Feed a delimiter to discard the garbage (will attempt decode → error).
    auto discardResult = dec.feedByte(0x00);
    // The garbage + delimiter won't form valid COBS, so either ERROR or INCOMPLETE.
    // If the garbage had no valid COBS: the decoder returns ERROR or tries and fails.

    // Feed a valid frame after re-sync.
    const uint8_t payload[] = {0x42};
    auto result = feedEncodedFrame(dec, MessageType::ACK, 10, payload, 1);
    EXPECT_EQ(result, DecoderResult::FRAME_READY);
    EXPECT_EQ(dec.getSeqNum(), 10u);
}

// ===========================================================================
// Partial accumulation
// ===========================================================================

TEST(FrameDecoderTest, PartialFrame_IncompleteUntilDelimiter)
{
    TestEncoder enc;
    TestDecoder dec;

    uint8_t wire[TestEncoder::kMaxWireSize];
    std::size_t n = enc.encode(MessageType::DATA, 3, nullptr, 0, wire, sizeof(wire));
    ASSERT_GT(n, 1u);

    // Feed all bytes except the last (delimiter).
    for (std::size_t i = 0; i < n - 1; ++i)
    {
        EXPECT_EQ(dec.feedByte(wire[i]), DecoderResult::INCOMPLETE);
    }

    // Feed the delimiter — frame should be ready.
    EXPECT_EQ(dec.feedByte(wire[n - 1]), DecoderResult::FRAME_READY);
}

// ===========================================================================
// Reset
// ===========================================================================

TEST(FrameDecoderTest, Reset_ClearsPartialState)
{
    TestDecoder dec;

    // Feed partial data.
    dec.feedByte(0x05);
    dec.feedByte(0x10);

    dec.reset();

    // After reset, a valid frame should decode correctly.
    auto result = feedEncodedFrame(dec, MessageType::HEARTBEAT, 0, nullptr, 0);
    EXPECT_EQ(result, DecoderResult::FRAME_READY);
}

// ===========================================================================
// Overflow protection
// ===========================================================================

TEST(FrameDecoderTest, Overflow_ReturnsError)
{
    FrameDecoder<4> dec; // Small max payload
    // Feed more bytes than the buffer can hold without hitting a delimiter.
    for (std::size_t i = 0; i < FrameDecoder<4>::kMaxCobsSize + 5; ++i)
    {
        auto r = dec.feedByte(0xAA);
        if (r == DecoderResult::ERROR)
        {
            // Good — overflow detected.
            return;
        }
    }
    FAIL() << "Expected ERROR due to buffer overflow";
}

// ===========================================================================
// Handshake frame roundtrip
// ===========================================================================

TEST(FrameDecoderTest, Roundtrip_HandshakeRequest)
{
    TestEncoder enc;
    TestDecoder dec;

    // HANDSHAKE_REQ payload: [version:2LE][magic:4]
    uint8_t payload[6];
    payload[0] = static_cast<uint8_t>(kProtocolVersion & 0xFF);
    payload[1] = static_cast<uint8_t>((kProtocolVersion >> 8) & 0xFF);
    payload[2] = static_cast<uint8_t>(kHandshakeMagic & 0xFF);
    payload[3] = static_cast<uint8_t>((kHandshakeMagic >> 8) & 0xFF);
    payload[4] = static_cast<uint8_t>((kHandshakeMagic >> 16) & 0xFF);
    payload[5] = static_cast<uint8_t>((kHandshakeMagic >> 24) & 0xFF);

    uint8_t wire[TestEncoder::kMaxWireSize];
    std::size_t n = enc.encode(MessageType::HANDSHAKE_REQ, 0, payload, 6, wire, sizeof(wire));
    ASSERT_GT(n, 0u);

    DecoderResult result = DecoderResult::INCOMPLETE;
    for (std::size_t i = 0; i < n; ++i)
    {
        result = dec.feedByte(wire[i]);
    }
    ASSERT_EQ(result, DecoderResult::FRAME_READY);
    EXPECT_EQ(dec.getMessageType(), MessageType::HANDSHAKE_REQ);
    ASSERT_EQ(dec.getPayloadLen(), 6u);

    // Verify magic.
    uint32_t magic = 0;
    std::memcpy(&magic, &dec.getPayload()[2], 4);
    EXPECT_EQ(magic, kHandshakeMagic);
}
