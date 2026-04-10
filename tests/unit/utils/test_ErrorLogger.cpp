/**
 * @file test_ErrorLogger.cpp
 * @brief Unit tests for ErrorLogger.
 *
 * Tests the ring-buffer storage, FIFO read order, overflow wrap semantics,
 * and the clear operation.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */

#include "sputteros/utils/logging/ErrorLogger.h"
#include <gtest/gtest.h>

using namespace SputterOS;
using EC = ErrorLogger::ErrorCode;

class ErrorLoggerTest : public ::testing::Test
{
  protected:
    ErrorLogger logger;
};

TEST_F(ErrorLoggerTest, InitialCount_IsZero) { EXPECT_EQ(logger.count(), 0u); }

TEST_F(ErrorLoggerTest, Read_OnEmptyBuffer_ReturnsFalse)
{
    ErrorLogger::Entry e{};
    EXPECT_FALSE(logger.read(e));
}

TEST_F(ErrorLoggerTest, LogOne_CountBecomesOne)
{
    logger.log(EC::HARD_FAULT, SputterMicros(1000), 0.0f);
    EXPECT_EQ(logger.count(), 1u);
}

TEST_F(ErrorLoggerTest, LogOne_ReadReturnsCorrectFields)
{
    logger.log(EC::INTERLOCK_TRIP, SputterMicros(5000), 3.14f);
    ErrorLogger::Entry e{};
    ASSERT_TRUE(logger.read(e));
    EXPECT_EQ(e.code, EC::INTERLOCK_TRIP);
    EXPECT_EQ(e.timestamp, SputterMicros(5000));
    EXPECT_NEAR(e.value, 3.14f, 1e-5f);
}

TEST_F(ErrorLoggerTest, LogOne_ReadDecrementsCount)
{
    logger.log(EC::SOFT_ABORT, SputterMicros(100));
    ErrorLogger::Entry e{};
    logger.read(e);
    EXPECT_EQ(logger.count(), 0u);
}

TEST_F(ErrorLoggerTest, MultipleEntries_ReadInFIFOOrder)
{
    logger.log(EC::STATE_TRANSITION, SputterMicros(1), 1.0f);
    logger.log(EC::ARC_DETECTED, SputterMicros(2), 2.0f);
    logger.log(EC::SENSOR_ERROR, SputterMicros(3), 3.0f);

    ErrorLogger::Entry e{};
    logger.read(e);
    EXPECT_EQ(e.timestamp, SputterMicros(1));
    logger.read(e);
    EXPECT_EQ(e.timestamp, SputterMicros(2));
    logger.read(e);
    EXPECT_EQ(e.timestamp, SputterMicros(3));
}

TEST_F(ErrorLoggerTest, FillToCapacity_AllEntriesReadableInOrder)
{
    constexpr std::size_t N = 16u;
    for (std::size_t i = 0; i < N; ++i)
        logger.log(EC::WATCHDOG_KICK, SputterMicros(static_cast<uint64_t>(i)), static_cast<float>(i));

    for (std::size_t i = 0; i < N; ++i)
    {
        ErrorLogger::Entry e{};
        ASSERT_TRUE(logger.read(e)) << "Failed at index " << i;
        EXPECT_EQ(e.timestamp, SputterMicros(static_cast<uint64_t>(i)));
    }
    EXPECT_EQ(logger.count(), 0u);
}

TEST_F(ErrorLoggerTest, Overflow_OldestEntryOverwritten)
{
    // ErrorLogger::kCapacity == 32; fill the buffer then overflow by one.
    for (std::size_t i = 0; i < 32u; ++i)
        logger.log(EC::WATCHDOG_KICK, SputterMicros(static_cast<uint64_t>(i)));

    logger.log(EC::HARD_FAULT, SputterMicros(9999));

    ErrorLogger::Entry e{};
    ASSERT_TRUE(logger.read(e));
    // Timestamp 0 was the oldest; it must have been dropped.
    EXPECT_EQ(e.timestamp, SputterMicros(1));
}

TEST_F(ErrorLoggerTest, Overflow_NewEntryIsPresent)
{
    // ErrorLogger::kCapacity == 32; fill then overflow by one.
    for (std::size_t i = 0; i < 32u; ++i)
        logger.log(EC::WATCHDOG_KICK, SputterMicros(static_cast<uint64_t>(i)));
    logger.log(EC::HARD_FAULT, SputterMicros(9999));

    ErrorLogger::Entry e{};
    ErrorLogger::Entry last{};
    while (logger.read(e))
    {
        last = e;
    }
    EXPECT_EQ(last.timestamp, SputterMicros(9999));
    EXPECT_EQ(last.code, EC::HARD_FAULT);
}

TEST_F(ErrorLoggerTest, Clear_ResetsCountToZero)
{
    logger.log(EC::SOFT_ABORT, SputterMicros(1));
    logger.log(EC::HARD_FAULT, SputterMicros(2));
    logger.clear();
    EXPECT_EQ(logger.count(), 0u);
}

TEST_F(ErrorLoggerTest, Clear_ReadReturnsFalseAfterClear)
{
    logger.log(EC::ARC_DETECTED, SputterMicros(1));
    logger.clear();
    ErrorLogger::Entry e{};
    EXPECT_FALSE(logger.read(e));
}

TEST_F(ErrorLoggerTest, Clear_NewEntriesAfterClearAreCorrect)
{
    logger.log(EC::SENSOR_ERROR, SputterMicros(1));
    logger.clear();
    logger.log(EC::WATCHDOG_KICK, SputterMicros(42), 7.7f);
    ErrorLogger::Entry e{};
    ASSERT_TRUE(logger.read(e));
    EXPECT_EQ(e.timestamp, SputterMicros(42));
    EXPECT_NEAR(e.value, 7.7f, 1e-5f);
}

TEST_F(ErrorLoggerTest, TwoInstances_IndependentState)
{
    ErrorLogger other;
    logger.log(EC::HARD_FAULT, SputterMicros(100));
    EXPECT_EQ(other.count(), 0u);
}
