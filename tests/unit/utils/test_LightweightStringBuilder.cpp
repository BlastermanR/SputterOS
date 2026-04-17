/**
 * @file test_LightweightStringBuilder.cpp
 * @brief Unit tests for LightweightStringBuilder.
 *
 * Verifies append overloads, chaining, capacity overflow truncation,
 * clear(), length(), and c_str() semantics.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/5/2026
 */

#include "sputteros/utils/logging/LightweightStringBuilder.h"
#include <cstring>
#include <gtest/gtest.h>

using namespace SputterOS;

class StringBuilderTest : public ::testing::Test
{
  protected:
    LightweightStringBuilder sb;
};

TEST_F(StringBuilderTest, InitialLength_IsZero) { EXPECT_EQ(sb.length(), 0u); }
TEST_F(StringBuilderTest, InitialCStr_IsEmptyString) { EXPECT_STREQ(sb.c_str(), ""); }
TEST_F(StringBuilderTest, InitialIsFull_IsFalse) { EXPECT_FALSE(sb.isFull()); }

TEST_F(StringBuilderTest, AppendLiteral_ProducesCorrectString)
{
    sb.append("hello");
    EXPECT_STREQ(sb.c_str(), "hello");
    EXPECT_EQ(sb.length(), 5u);
}

TEST_F(StringBuilderTest, AppendLiteral_EmptyString_NoChange)
{
    sb.append("abc");
    sb.append("");
    EXPECT_STREQ(sb.c_str(), "abc");
}

TEST_F(StringBuilderTest, AppendLiteral_NullPointer_NoChange)
{
    sb.append("x");
    sb.append(static_cast<const char *>(nullptr));
    EXPECT_STREQ(sb.c_str(), "x");
}

TEST_F(StringBuilderTest, AppendInt_PositiveValue)
{
    sb.append(int32_t(42));
    EXPECT_STREQ(sb.c_str(), "42");
}
TEST_F(StringBuilderTest, AppendInt_NegativeValue)
{
    sb.append(int32_t(-7));
    EXPECT_STREQ(sb.c_str(), "-7");
}
TEST_F(StringBuilderTest, AppendInt_Zero)
{
    sb.append(int32_t(0));
    EXPECT_STREQ(sb.c_str(), "0");
}
TEST_F(StringBuilderTest, AppendUint_Value)
{
    sb.append(uint32_t(123u));
    EXPECT_STREQ(sb.c_str(), "123");
}

TEST_F(StringBuilderTest, AppendFloat_DefaultDecimals_ThreeDecimalPlaces)
{
    sb.append(3.14159f);
    EXPECT_STREQ(sb.c_str(), "3.142");
}

TEST_F(StringBuilderTest, AppendFloat_ZeroDecimals_TruncatesFraction)
{
    sb.append(5.9f, uint8_t(0));
    EXPECT_STREQ(sb.c_str(), "6");
}

TEST_F(StringBuilderTest, AppendChar_SingleCharacter)
{
    sb.append('!');
    EXPECT_STREQ(sb.c_str(), "!");
    EXPECT_EQ(sb.length(), 1u);
}

TEST_F(StringBuilderTest, Chaining_ProducesCorrectConcatenation)
{
    sb.append("STATE:").append(int32_t(3)).append(",P:").append(0.001f, uint8_t(3)).append("\n");
    EXPECT_STREQ(sb.c_str(), "STATE:3,P:0.001\n");
}

TEST_F(StringBuilderTest, Overflow_StringTruncatedAtCapacity)
{
    char big[201];
    memset(big, 'A', 200);
    big[200] = '\0';
    sb.append(big);
    EXPECT_LE(sb.length(), LightweightStringBuilder::kCapacity - 1);
    EXPECT_EQ(sb.c_str()[sb.length()], '\0');
}

TEST_F(StringBuilderTest, Overflow_IsFull_TrueAfterOverflow)
{
    char big[201];
    memset(big, 'B', 200);
    big[200] = '\0';
    sb.append(big);
    EXPECT_TRUE(sb.isFull());
}

TEST_F(StringBuilderTest, Clear_ResetsLengthToZero)
{
    sb.append("data");
    sb.clear();
    EXPECT_EQ(sb.length(), 0u);
}

TEST_F(StringBuilderTest, Clear_CStrIsEmpty)
{
    sb.append("data");
    sb.clear();
    EXPECT_STREQ(sb.c_str(), "");
}

TEST_F(StringBuilderTest, Clear_AppendAfterClearWorksNormally)
{
    sb.append("old");
    sb.clear();
    sb.append("new");
    EXPECT_STREQ(sb.c_str(), "new");
}
