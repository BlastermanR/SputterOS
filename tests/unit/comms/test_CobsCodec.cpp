/**
 * @file test_CobsCodec.cpp
 * @brief Unit tests for COBS encode/decode functions.
 *
 * Validates roundtrip integrity, edge cases (empty, all-zeros, all-0xFF,
 * max-length blocks), and error handling (buffer too small, malformed input).
 *
 * @author SputterOS Contributors
 * @date 4/12/2026
 */

#include "sputteros/comms/protocol/CobsCodec.h"
#include <array>
#include <cstring>
#include <gtest/gtest.h>
#include <vector>

using namespace SputterOS;

// ===========================================================================
// Helpers
// ===========================================================================

/** @brief Roundtrip encode→decode and verify output matches input. */
static void verifyRoundtrip(const uint8_t *data, std::size_t len)
{
    const std::size_t    encCap = cobsMaxEncodedLen(len) + 16;
    std::vector<uint8_t> encoded(encCap, 0xCC);
    std::vector<uint8_t> decoded(len + 16, 0xCC);

    std::size_t encLen = Cobs::encode(data, len, encoded.data(), encCap);
    ASSERT_GT(encLen, 0u) << "Encode failed for input of length " << len;

    // Verify no 0x00 bytes in encoded output.
    for (std::size_t i = 0; i < encLen; ++i)
    {
        EXPECT_NE(encoded[i], 0x00) << "Zero byte at encoded offset " << i;
    }

    std::size_t decLen = Cobs::decode(encoded.data(), encLen, decoded.data(), len + 16);
    ASSERT_EQ(decLen, len) << "Decoded length mismatch";
    EXPECT_EQ(std::memcmp(data, decoded.data(), len), 0) << "Decoded data mismatch";
}

// ===========================================================================
// Roundtrip tests
// ===========================================================================

TEST(CobsCodecTest, Roundtrip_SingleByte)
{
    const uint8_t data[] = {0x42};
    verifyRoundtrip(data, 1);
}

TEST(CobsCodecTest, Roundtrip_SingleZero)
{
    const uint8_t data[] = {0x00};
    verifyRoundtrip(data, 1);
}

TEST(CobsCodecTest, Roundtrip_AllZeros)
{
    uint8_t data[10];
    std::memset(data, 0x00, sizeof(data));
    verifyRoundtrip(data, sizeof(data));
}

TEST(CobsCodecTest, Roundtrip_AllFF)
{
    uint8_t data[10];
    std::memset(data, 0xFF, sizeof(data));
    verifyRoundtrip(data, sizeof(data));
}

TEST(CobsCodecTest, Roundtrip_MixedData)
{
    const uint8_t data[] = {0x01, 0x00, 0x02, 0x00, 0x03, 0x04, 0x00, 0x05};
    verifyRoundtrip(data, sizeof(data));
}

TEST(CobsCodecTest, Roundtrip_254NonZeroBytes)
{
    // Exactly 254 non-zero bytes — fills one COBS block.
    uint8_t data[254];
    for (int i = 0; i < 254; ++i)
    {
        data[i] = static_cast<uint8_t>((i % 254) + 1);
    }
    verifyRoundtrip(data, 254);
}

TEST(CobsCodecTest, Roundtrip_255NonZeroBytes)
{
    // 255 non-zero bytes — triggers a block boundary.
    uint8_t data[255];
    for (int i = 0; i < 255; ++i)
    {
        data[i] = static_cast<uint8_t>((i % 254) + 1);
    }
    verifyRoundtrip(data, 255);
}

TEST(CobsCodecTest, Roundtrip_LargePayload)
{
    uint8_t data[512];
    for (int i = 0; i < 512; ++i)
    {
        data[i] = static_cast<uint8_t>(i & 0xFF);
    }
    verifyRoundtrip(data, 512);
}

// ===========================================================================
// Known-vector tests (from COBS specification)
// ===========================================================================

TEST(CobsCodecTest, KnownVector_Empty)
{
    // Empty input encodes to a single overhead byte.
    uint8_t     encoded[2];
    std::size_t n = Cobs::encode(nullptr, 0, encoded, 2);
    ASSERT_EQ(n, 1u);
    EXPECT_EQ(encoded[0], 0x01);

    uint8_t     decoded[1];
    std::size_t d = Cobs::decode(encoded, n, decoded, 0);
    EXPECT_EQ(d, 0u);
}

TEST(CobsCodecTest, KnownVector_00)
{
    // Input: [0x00]  ⇒  encoded: [0x01, 0x01]
    const uint8_t in[] = {0x00};
    uint8_t       enc[4];
    std::size_t   n = Cobs::encode(in, 1, enc, 4);
    ASSERT_EQ(n, 2u);
    EXPECT_EQ(enc[0], 0x01);
    EXPECT_EQ(enc[1], 0x01);
}

TEST(CobsCodecTest, KnownVector_00_00)
{
    // Input: [0x00, 0x00]  ⇒  encoded: [0x01, 0x01, 0x01]
    const uint8_t in[] = {0x00, 0x00};
    uint8_t       enc[5];
    std::size_t   n = Cobs::encode(in, 2, enc, 5);
    ASSERT_EQ(n, 3u);
    EXPECT_EQ(enc[0], 0x01);
    EXPECT_EQ(enc[1], 0x01);
    EXPECT_EQ(enc[2], 0x01);
}

TEST(CobsCodecTest, KnownVector_11_22_00_33)
{
    // Input: [0x11, 0x22, 0x00, 0x33] ⇒ encoded: [0x03, 0x11, 0x22, 0x02, 0x33]
    const uint8_t in[] = {0x11, 0x22, 0x00, 0x33};
    uint8_t       enc[8];
    std::size_t   n = Cobs::encode(in, 4, enc, 8);
    ASSERT_EQ(n, 5u);
    EXPECT_EQ(enc[0], 0x03);
    EXPECT_EQ(enc[1], 0x11);
    EXPECT_EQ(enc[2], 0x22);
    EXPECT_EQ(enc[3], 0x02);
    EXPECT_EQ(enc[4], 0x33);
}

// ===========================================================================
// Error handling
// ===========================================================================

TEST(CobsCodecTest, Encode_BufferTooSmall_ReturnsZero)
{
    const uint8_t data[] = {0x01, 0x02, 0x03};
    uint8_t       enc[1]; // Way too small
    EXPECT_EQ(Cobs::encode(data, 3, enc, 1), 0u);
}

TEST(CobsCodecTest, Decode_EmptyInput_ReturnsZero)
{
    uint8_t out[4];
    EXPECT_EQ(Cobs::decode(nullptr, 0, out, 4), 0u);
}

TEST(CobsCodecTest, Decode_ZeroInEncodedStream_ReturnsZero)
{
    // 0x00 in encoded stream is illegal.
    const uint8_t bad[] = {0x03, 0x00, 0x01};
    uint8_t       out[4];
    EXPECT_EQ(Cobs::decode(bad, 3, out, 4), 0u);
}

TEST(CobsCodecTest, Decode_OutputBufferTooSmall_ReturnsZero)
{
    const uint8_t in[] = {0x01, 0x02, 0x03};
    uint8_t       enc[8];
    Cobs::encode(in, 3, enc, 8);

    uint8_t out[1]; // Too small
    EXPECT_EQ(Cobs::decode(enc, Cobs::encode(in, 3, enc, 8), out, 1), 0u);
}

// ===========================================================================
// cobsMaxEncodedLen
// ===========================================================================

TEST(CobsCodecTest, MaxEncodedLen_Zero) { EXPECT_EQ(cobsMaxEncodedLen(0), 1u); }

TEST(CobsCodecTest, MaxEncodedLen_254)
{
    // 254 bytes ⇒ 254 + 254/254 + 1 = 256
    EXPECT_EQ(cobsMaxEncodedLen(254), 256u);
}

TEST(CobsCodecTest, MaxEncodedLen_1) { EXPECT_EQ(cobsMaxEncodedLen(1), 2u); }
