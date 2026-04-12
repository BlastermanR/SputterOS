/**
 * @file test_ResponseSerializer.cpp
 * @brief Unit tests for ResponseSerializer payload serialization functions.
 *
 * Validates each serializer produces correctly structured payloads and
 * handles buffer-too-small / null-pointer errors gracefully.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/12/2026
 */

#include "sputteros/comms/protocol/FrameConstants.h"
#include "sputteros/comms/protocol/ResponseSerializer.h"
#include <cstring>
#include <gtest/gtest.h>

using namespace SputterOS;

// ===========================================================================
// serializeAck
// ===========================================================================

TEST(ResponseSerializerTest, SerializeAck_Valid)
{
    uint8_t buf[8] = {};
    std::size_t n = ResponseSerializer::serializeAck(0x03, buf, sizeof(buf));
    EXPECT_EQ(n, 1u);
    EXPECT_EQ(buf[0], 0x03);
}

TEST(ResponseSerializerTest, SerializeAck_BufferTooSmall_ReturnsZero)
{
    uint8_t buf[0];
    EXPECT_EQ(ResponseSerializer::serializeAck(1, buf, 0), 0u);
}

// ===========================================================================
// serializeNack
// ===========================================================================

TEST(ResponseSerializerTest, SerializeNack_Valid)
{
    uint8_t buf[8] = {};
    float val = 99.5f;
    std::size_t n = ResponseSerializer::serializeNack(2, 5, val, buf, sizeof(buf));
    EXPECT_EQ(n, 6u);
    EXPECT_EQ(buf[0], 2);
    EXPECT_EQ(buf[1], 5);

    float decoded;
    std::memcpy(&decoded, &buf[2], sizeof(float));
    EXPECT_FLOAT_EQ(decoded, 99.5f);
}

TEST(ResponseSerializerTest, SerializeNack_BufferTooSmall_ReturnsZero)
{
    uint8_t buf[4] = {};
    EXPECT_EQ(ResponseSerializer::serializeNack(1, 0, 0.0f, buf, 4), 0u);
}

// ===========================================================================
// serializeHandshakeResp
// ===========================================================================

TEST(ResponseSerializerTest, SerializeHandshakeResp_Valid)
{
    uint8_t buf[8] = {};
    std::size_t n = ResponseSerializer::serializeHandshakeResp(buf, sizeof(buf));
    EXPECT_EQ(n, 6u);

    // Verify version
    uint16_t version = static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
    EXPECT_EQ(version, kProtocolVersion);

    // Verify magic
    uint32_t magic = static_cast<uint32_t>(buf[2]) |
                     (static_cast<uint32_t>(buf[3]) << 8) |
                     (static_cast<uint32_t>(buf[4]) << 16) |
                     (static_cast<uint32_t>(buf[5]) << 24);
    EXPECT_EQ(magic, kHandshakeMagic);
}

TEST(ResponseSerializerTest, SerializeHandshakeResp_BufferTooSmall_ReturnsZero)
{
    uint8_t buf[4] = {};
    EXPECT_EQ(ResponseSerializer::serializeHandshakeResp(buf, 4), 0u);
}

// ===========================================================================
// serializeCommand
// ===========================================================================

TEST(ResponseSerializerTest, SerializeCommand_Valid)
{
    uint8_t buf[8] = {};
    float val = -1.5f;
    std::size_t n = ResponseSerializer::serializeCommand(7, 2, val, buf, sizeof(buf));
    EXPECT_EQ(n, 6u);
    EXPECT_EQ(buf[0], 7);
    EXPECT_EQ(buf[1], 2);

    float decoded;
    std::memcpy(&decoded, &buf[2], sizeof(float));
    EXPECT_FLOAT_EQ(decoded, -1.5f);
}

TEST(ResponseSerializerTest, SerializeCommand_BufferTooSmall_ReturnsZero)
{
    uint8_t buf[2] = {};
    EXPECT_EQ(ResponseSerializer::serializeCommand(0, 0, 0.0f, buf, 2), 0u);
}

// ===========================================================================
// serializeData
// ===========================================================================

TEST(ResponseSerializerTest, SerializeData_Valid)
{
    const uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t buf[8] = {};
    std::size_t n = ResponseSerializer::serializeData(data, 4, buf, sizeof(buf));
    EXPECT_EQ(n, 4u);
    EXPECT_EQ(buf[0], 0xDE);
    EXPECT_EQ(buf[3], 0xEF);
}

TEST(ResponseSerializerTest, SerializeData_Empty)
{
    uint8_t buf[8] = {};
    EXPECT_EQ(ResponseSerializer::serializeData(nullptr, 0, buf, sizeof(buf)), 0u);
}

TEST(ResponseSerializerTest, SerializeData_BufferTooSmall_ReturnsZero)
{
    const uint8_t data[] = {1, 2, 3, 4};
    uint8_t buf[2] = {};
    EXPECT_EQ(ResponseSerializer::serializeData(data, 4, buf, 2), 0u);
}

TEST(ResponseSerializerTest, SerializeData_NullData_NonZeroLen_ReturnsZero)
{
    uint8_t buf[8] = {};
    EXPECT_EQ(ResponseSerializer::serializeData(nullptr, 5, buf, sizeof(buf)), 0u);
}

// ===========================================================================
// serializeLog
// ===========================================================================

TEST(ResponseSerializerTest, SerializeLog_Valid)
{
    uint8_t buf[64] = {};
    std::size_t n = ResponseSerializer::serializeLog(2, "hello", buf, sizeof(buf));
    EXPECT_EQ(n, 6u); // 1 (level) + 5 (text)
    EXPECT_EQ(buf[0], 2);
    EXPECT_EQ(buf[1], 'h');
    EXPECT_EQ(buf[5], 'o');
}

TEST(ResponseSerializerTest, SerializeLog_NullText_ReturnsZero)
{
    uint8_t buf[8] = {};
    EXPECT_EQ(ResponseSerializer::serializeLog(0, nullptr, buf, sizeof(buf)), 0u);
}

TEST(ResponseSerializerTest, SerializeLog_BufferTooSmall_ReturnsZero)
{
    uint8_t buf[3] = {};
    EXPECT_EQ(ResponseSerializer::serializeLog(0, "toolong", buf, 3), 0u);
}

TEST(ResponseSerializerTest, SerializeLog_EmptyString)
{
    uint8_t buf[8] = {};
    std::size_t n = ResponseSerializer::serializeLog(1, "", buf, sizeof(buf));
    EXPECT_EQ(n, 1u); // Just the level byte
    EXPECT_EQ(buf[0], 1);
}
