/**
 * @file test_Crc16.cpp
 * @brief Unit tests for CRC16-CCITT implementation.
 *
 * Validates against known test vectors, edge cases, and incremental
 * computation.
 *
 * @author SputterOS Contributors
 * @date 4/12/2026
 */

#include "sputteros/comms/protocol/Crc16.h"
#include <cstring>
#include <gtest/gtest.h>

using namespace SputterOS;

// ===========================================================================
// Known-vector tests
// ===========================================================================

TEST(Crc16Test, KnownVector_123456789)
{
    // Standard CRC16-CCITT test vector: "123456789" ⇒ 0x29B1
    const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_EQ(crc16(data, sizeof(data)), 0x29B1);
}

TEST(Crc16Test, EmptyInput_ReturnsInit)
{
    EXPECT_EQ(crc16(nullptr, 0), 0xFFFF);
    EXPECT_EQ(crc16(nullptr, 0, 0x0000), 0x0000);
}

TEST(Crc16Test, SingleByte_Zero)
{
    const uint8_t data[] = {0x00};
    uint16_t      result = crc16(data, 1);
    // Not the init value — CRC was computed.
    EXPECT_NE(result, 0xFFFF);
}

TEST(Crc16Test, SingleByte_FF)
{
    const uint8_t data[] = {0xFF};
    uint16_t      result = crc16(data, 1);
    EXPECT_NE(result, 0xFFFF);
}

// ===========================================================================
// Incremental computation
// ===========================================================================

TEST(Crc16Test, Incremental_MatchesSinglePass)
{
    const uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};

    uint16_t singlePass = crc16(data, 5);

    // Compute in two increments.
    uint16_t crc = crc16(data, 3);
    crc          = crc16(data + 3, 2, crc);

    EXPECT_EQ(crc, singlePass);
}

// ===========================================================================
// Custom init values
// ===========================================================================

TEST(Crc16Test, CustomInit_DifferentResult)
{
    const uint8_t data[] = {0xAA, 0xBB};
    uint16_t      a      = crc16(data, 2, 0xFFFF);
    uint16_t      b      = crc16(data, 2, 0x0000);
    EXPECT_NE(a, b);
}

// ===========================================================================
// Determinism
// ===========================================================================

TEST(Crc16Test, Deterministic_SameInputSameOutput)
{
    const uint8_t data[] = {0x10, 0x20, 0x30};
    uint16_t      a      = crc16(data, 3);
    uint16_t      b      = crc16(data, 3);
    EXPECT_EQ(a, b);
}
