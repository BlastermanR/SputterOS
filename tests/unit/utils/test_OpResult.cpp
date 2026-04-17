/**
 * @file test_OpResult.cpp
 * @brief Unit tests for OpResult enum and succeeded() helper.
 *
 * Validates all enum values, the `succeeded()` convenience check,
 * and comparison semantics.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "sputteros/OpResult.h"
#include <gtest/gtest.h>

using namespace SputterOS;

// ===========================================================================
// Enum values
// ===========================================================================

TEST(OpResultTest, OK_HasValueZero) { EXPECT_EQ(static_cast<uint8_t>(OpResult::OK), 0u); }

TEST(OpResultTest, FULL_HasValueOne) { EXPECT_EQ(static_cast<uint8_t>(OpResult::FULL), 1u); }

TEST(OpResultTest, NULL_ARG_HasValueTwo) { EXPECT_EQ(static_cast<uint8_t>(OpResult::NULL_ARG), 2u); }

// ===========================================================================
// succeeded() helper
// ===========================================================================

TEST(OpResultTest, Succeeded_OK_ReturnsTrue) { EXPECT_TRUE(succeeded(OpResult::OK)); }

TEST(OpResultTest, Succeeded_FULL_ReturnsFalse) { EXPECT_FALSE(succeeded(OpResult::FULL)); }

TEST(OpResultTest, Succeeded_NULL_ARG_ReturnsFalse) { EXPECT_FALSE(succeeded(OpResult::NULL_ARG)); }

// ===========================================================================
// Equality / Inequality
// ===========================================================================

TEST(OpResultTest, Equality_SameValues)
{
    OpResult a = OpResult::OK;
    OpResult b = OpResult::OK;
    EXPECT_EQ(a, b);
}

TEST(OpResultTest, Inequality_DifferentValues)
{
    EXPECT_NE(OpResult::OK, OpResult::FULL);
    EXPECT_NE(OpResult::OK, OpResult::NULL_ARG);
    EXPECT_NE(OpResult::FULL, OpResult::NULL_ARG);
}

// ===========================================================================
// Usage in conditional context
// ===========================================================================

TEST(OpResultTest, ConditionalUsage_CompareAgainstOK)
{
    OpResult result = OpResult::OK;
    if (result != OpResult::OK)
    {
        FAIL() << "OpResult::OK should compare equal to itself";
    }
}
