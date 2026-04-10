/**
 * @file test_TelemetryLogger.cpp
 * @brief Unit tests for TelemetryLogger.
 *
 * Covers ring-buffer semantics, FIFO drain order, verbosity filtering,
 * buffer overflow wrapping, output format, task label strings, and the
 * clear operation. A capturing write callback is used to inspect formatted
 * output without requiring a real stream.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */

#include "sputteros/utils/logging/TelemetryLogger.h"

#include <algorithm>
#include <cstring>
#include <gtest/gtest.h>
#include <string>

using namespace SputterOS;

/// Helper: convert millisecond-level values to SputterMicros for readability.
static constexpr SputterMicros ms(uint64_t milliseconds) { return milliseconds * 1000u; }

using TL  = TelemetryLogger;
using VL  = TL::Verbosity;
using TID = TL::TaskID;

// ===========================================================================
// Helpers
// ===========================================================================

/**
 * @brief DrainWriteFn callback that appends all bytes to a std::string.
 *
 * Pass as the write callback to drain(), with ctx pointing to a std::string.
 */
static void captureWrite(const uint8_t *data, std::size_t len, void *ctx)
{
    static_cast<std::string *>(ctx)->append(reinterpret_cast<const char *>(data), len);
}

// ===========================================================================
// Fixture
// ===========================================================================

class TelemetryLoggerTest : public ::testing::Test
{
  protected:
    TelemetryLogger logger;
    std::string     output;
};

// ===========================================================================
// Initial state
// ===========================================================================

TEST_F(TelemetryLoggerTest, InitialCount_IsZero) { EXPECT_EQ(logger.count(), 0u); }

TEST_F(TelemetryLoggerTest, InitialVerbosity_IsStatus) { EXPECT_EQ(logger.getVerbosity(), VL::STATUS); }

TEST_F(TelemetryLoggerTest, DrainOnEmpty_WritesNothing)
{
    logger.drain(captureWrite, &output);
    EXPECT_TRUE(output.empty());
}

// ===========================================================================
// log() and count()
// ===========================================================================

TEST_F(TelemetryLoggerTest, LogOne_CountBecomesOne)
{
    logger.log(TID::CONTROL, "hello", VL::STATUS, ms(1000));
    EXPECT_EQ(logger.count(), 1u);
}

TEST_F(TelemetryLoggerTest, LogMultiple_CountAccumulates)
{
    logger.log(TID::CONTROL, "a", VL::STATUS, ms(1));
    logger.log(TID::COMMS, "b", VL::STATUS, ms(2));
    logger.log(TID::DIAGNOSTICS, "c", VL::STATUS, ms(3));
    EXPECT_EQ(logger.count(), 3u);
}

// ===========================================================================
// drain() - FIFO order and buffer drain
// ===========================================================================

TEST_F(TelemetryLoggerTest, DrainOne_CountBecomesZero)
{
    logger.log(TID::CONTROL, "msg", VL::STATUS, ms(100));
    logger.drain(captureWrite, &output);
    EXPECT_EQ(logger.count(), 0u);
}

TEST_F(TelemetryLoggerTest, DrainMultiple_AllEntriesDrained)
{
    logger.log(TID::CONTROL, "first", VL::STATUS, ms(1));
    logger.log(TID::COMMS, "second", VL::STATUS, ms(2));
    logger.log(TID::DIAGNOSTICS, "third", VL::STATUS, ms(3));
    logger.drain(captureWrite, &output);
    EXPECT_EQ(logger.count(), 0u);
}

TEST_F(TelemetryLoggerTest, DrainOrder_FIFO)
{
    // Log in order and verify timestamps appear in that order in the output.
    logger.log(TID::SYSTEM, "first", VL::STATUS, ms(10));
    logger.log(TID::SYSTEM, "second", VL::STATUS, ms(20));
    logger.log(TID::SYSTEM, "third", VL::STATUS, ms(30));
    logger.drain(captureWrite, &output);

    const std::size_t pos1 = output.find("first");
    const std::size_t pos2 = output.find("second");
    const std::size_t pos3 = output.find("third");

    ASSERT_NE(pos1, std::string::npos);
    ASSERT_NE(pos2, std::string::npos);
    ASSERT_NE(pos3, std::string::npos);

    EXPECT_LT(pos1, pos2);
    EXPECT_LT(pos2, pos3);
}

// ===========================================================================
// Output format
// ===========================================================================

TEST_F(TelemetryLoggerTest, OutputFormat_ContainsTimestamp)
{
    logger.log(TID::CONTROL, "test", VL::STATUS, ms(12345));
    logger.drain(captureWrite, &output);
    EXPECT_NE(output.find("12345"), std::string::npos);
}

TEST_F(TelemetryLoggerTest, OutputFormat_ContainsMessage)
{
    logger.log(TID::CONTROL, "my message", VL::STATUS, ms(1));
    logger.drain(captureWrite, &output);
    EXPECT_NE(output.find("my message"), std::string::npos);
}

TEST_F(TelemetryLoggerTest, OutputFormat_EndsWithNewline)
{
    logger.log(TID::CONTROL, "line", VL::STATUS, ms(1));
    logger.drain(captureWrite, &output);
    ASSERT_FALSE(output.empty());
    EXPECT_EQ(output.back(), '\n');
}

TEST_F(TelemetryLoggerTest, OutputFormat_ContainsTaskLabel)
{
    logger.log(TID::CONTROL, "tick", VL::STATUS, ms(1));
    logger.drain(captureWrite, &output);
    EXPECT_NE(output.find("ControlTask"), std::string::npos);
}

// ===========================================================================
// Task label strings
// ===========================================================================

TEST_F(TelemetryLoggerTest, TaskLabel_Control_IsControlTask)
{
    logger.log(TID::CONTROL, "x", VL::STATUS, ms(1));
    logger.drain(captureWrite, &output);
    EXPECT_NE(output.find("ControlTask"), std::string::npos);
}

TEST_F(TelemetryLoggerTest, TaskLabel_Comms_IsCommsTask)
{
    logger.log(TID::COMMS, "x", VL::STATUS, ms(1));
    logger.drain(captureWrite, &output);
    EXPECT_NE(output.find("CommsTask"), std::string::npos);
}

TEST_F(TelemetryLoggerTest, TaskLabel_Diagnostics_IsDiagnosticsTask)
{
    logger.log(TID::DIAGNOSTICS, "x", VL::STATUS, ms(1));
    logger.drain(captureWrite, &output);
    EXPECT_NE(output.find("DiagnosticsTask"), std::string::npos);
}

TEST_F(TelemetryLoggerTest, TaskLabel_System_IsSystem)
{
    logger.log(TID::SYSTEM, "x", VL::STATUS, ms(1));
    logger.drain(captureWrite, &output);
    EXPECT_NE(output.find("System"), std::string::npos);
}

// ===========================================================================
// Verbosity filtering
// ===========================================================================

TEST_F(TelemetryLoggerTest, VerbosityFilter_DebugSuppressedAtStatusThreshold)
{
    logger.setVerbosity(VL::STATUS);
    logger.log(TID::CONTROL, "debug-msg", VL::DEBUG, ms(1));
    logger.drain(captureWrite, &output);
    EXPECT_EQ(output.find("debug-msg"), std::string::npos);
}

TEST_F(TelemetryLoggerTest, VerbosityFilter_DebugPassesAtDebugThreshold)
{
    logger.setVerbosity(VL::DEBUG);
    logger.log(TID::CONTROL, "debug-msg", VL::DEBUG, ms(1));
    logger.drain(captureWrite, &output);
    EXPECT_NE(output.find("debug-msg"), std::string::npos);
}

TEST_F(TelemetryLoggerTest, VerbosityFilter_CriticalAlwaysPasses)
{
    logger.setVerbosity(VL::CRITICAL);
    logger.log(TID::SYSTEM, "critical-msg", VL::CRITICAL, ms(1));
    logger.drain(captureWrite, &output);
    EXPECT_NE(output.find("critical-msg"), std::string::npos);
}

TEST_F(TelemetryLoggerTest, VerbosityFilter_StatusSuppressedAtCritical)
{
    logger.setVerbosity(VL::CRITICAL);
    logger.log(TID::SYSTEM, "status-msg", VL::STATUS, ms(1));
    logger.drain(captureWrite, &output);
    EXPECT_EQ(output.find("status-msg"), std::string::npos);
}

TEST_F(TelemetryLoggerTest, VerbosityFilter_SuppressedEntriesStillDrainBuffer)
{
    // Suppressed entries must still be consumed so the buffer does not fill.
    logger.setVerbosity(VL::CRITICAL);
    logger.log(TID::CONTROL, "loud", VL::DEBUG, ms(1));
    logger.drain(captureWrite, &output);
    EXPECT_EQ(logger.count(), 0u);
}

// ===========================================================================
// setVerbosity / getVerbosity
// ===========================================================================

TEST_F(TelemetryLoggerTest, SetVerbosity_GetVerbosityReflectsChange)
{
    logger.setVerbosity(VL::DEBUG);
    EXPECT_EQ(logger.getVerbosity(), VL::DEBUG);

    logger.setVerbosity(VL::CRITICAL);
    EXPECT_EQ(logger.getVerbosity(), VL::CRITICAL);
}

// ===========================================================================
// clear()
// ===========================================================================

TEST_F(TelemetryLoggerTest, Clear_ResetsCount)
{
    logger.log(TID::CONTROL, "a", VL::STATUS, ms(1));
    logger.log(TID::CONTROL, "b", VL::STATUS, ms(2));
    logger.clear();
    EXPECT_EQ(logger.count(), 0u);
}

TEST_F(TelemetryLoggerTest, Clear_DrainWritesNothingAfterClear)
{
    logger.log(TID::CONTROL, "keep-quiet", VL::STATUS, ms(1));
    logger.clear();
    logger.drain(captureWrite, &output);
    EXPECT_TRUE(output.empty());
}

// ===========================================================================
// Ring buffer overflow
// ===========================================================================

TEST_F(TelemetryLoggerTest, Overflow_CountClampedAtCapacity)
{
    for (std::size_t i = 0; i < TL::kCapacity + 5; ++i)
        logger.log(TID::CONTROL, "x", VL::STATUS, ms(static_cast<int64_t>(i)));

    EXPECT_EQ(logger.count(), TL::kCapacity);
}

TEST_F(TelemetryLoggerTest, Overflow_WrapsAndRetainsLatestEntries)
{
    // Fill beyond capacity; newest entries should survive.
    for (std::size_t i = 0; i < TL::kCapacity; ++i)
        logger.log(TID::CONTROL, "old", VL::STATUS, ms(static_cast<int64_t>(i)));

    logger.log(TID::CONTROL, "newest", VL::STATUS, ms(9999));
    logger.drain(captureWrite, &output);

    EXPECT_NE(output.find("newest"), std::string::npos);
}

// ===========================================================================
// Text truncation
// ===========================================================================

TEST_F(TelemetryLoggerTest, LongText_TruncatedToCapacity)
{
    // Build a string longer than kTextLen.
    std::string longMsg(TL::kTextLen + 20, 'A');
    logger.log(TID::CONTROL, longMsg.c_str(), VL::STATUS, ms(1));
    // Must not crash and count is still 1.
    EXPECT_EQ(logger.count(), 1u);
    logger.drain(captureWrite, &output);
    // Output must contain at most the first kTextLen-1 'A' characters.
    const std::size_t aCount = std::count(output.begin(), output.end(), 'A');
    EXPECT_LE(aCount, TL::kTextLen - 1);
}

// ===========================================================================
// Mixed verbosity levels in a single drain pass
// ===========================================================================

TEST_F(TelemetryLoggerTest, MixedVerbosity_OnlyPassingLevelsPrinted)
{
    logger.setVerbosity(VL::INFO);
    logger.log(TID::CONTROL, "critical-line", VL::CRITICAL, ms(1));
    logger.log(TID::COMMS, "info-line", VL::INFO, ms(2));
    logger.log(TID::DIAGNOSTICS, "debug-line", VL::DEBUG, ms(3));
    logger.drain(captureWrite, &output);

    EXPECT_NE(output.find("critical-line"), std::string::npos);
    EXPECT_NE(output.find("info-line"), std::string::npos);
    EXPECT_EQ(output.find("debug-line"), std::string::npos);
}

// ===========================================================================
// Multiple drain calls
// ===========================================================================

TEST_F(TelemetryLoggerTest, SequentialDrains_SecondDrainWritesNothing)
{
    logger.log(TID::CONTROL, "once", VL::STATUS, ms(1));
    logger.drain(captureWrite, &output);
    output.clear();

    logger.drain(captureWrite, &output);
    EXPECT_TRUE(output.empty());
}

// ===========================================================================
// v0.2.0 — Optional IMutex guard coverage
// ===========================================================================

/**
 * @brief Counting IMutex stub that tracks lock/unlock call counts.
 */
class CountingMutex : public SputterOS::IMutex
{
  public:
    bool lock(std::chrono::milliseconds /*timeout*/) override
    {
        ++lockCount;
        return true;
    }

    void unlock() override { ++unlockCount; }

    int lockCount   = 0;
    int unlockCount = 0;
};

TEST(TelemetryLoggerMutex, Log_CallsLockAndUnlock)
{
    CountingMutex   mtx;
    TelemetryLogger guarded(&mtx);

    guarded.log(TID::CONTROL, "guarded", VL::STATUS, ms(1));
    EXPECT_GE(mtx.lockCount, 1);
    EXPECT_EQ(mtx.lockCount, mtx.unlockCount);
}

TEST(TelemetryLoggerMutex, Drain_CallsLockAndUnlock)
{
    CountingMutex   mtx;
    TelemetryLogger guarded(&mtx);
    std::string     out;

    guarded.log(TID::CONTROL, "drain-me", VL::STATUS, ms(1));
    int preLock = mtx.lockCount;

    guarded.drain(captureWrite, &out);
    EXPECT_GT(mtx.lockCount, preLock);
    EXPECT_EQ(mtx.lockCount, mtx.unlockCount);
}

TEST(TelemetryLoggerMutex, Count_CallsLockAndUnlock)
{
    CountingMutex   mtx;
    TelemetryLogger guarded(&mtx);

    guarded.log(TID::CONTROL, "x", VL::STATUS, ms(1));
    int preLock = mtx.lockCount;

    guarded.count();
    EXPECT_GT(mtx.lockCount, preLock);
    EXPECT_EQ(mtx.lockCount, mtx.unlockCount);
}

TEST(TelemetryLoggerMutex, Clear_CallsLockAndUnlock)
{
    CountingMutex   mtx;
    TelemetryLogger guarded(&mtx);

    guarded.log(TID::CONTROL, "x", VL::STATUS, ms(1));
    int preLock = mtx.lockCount;

    guarded.clear();
    EXPECT_GT(mtx.lockCount, preLock);
    EXPECT_EQ(mtx.lockCount, mtx.unlockCount);
}

// ===========================================================================
// v0.2.0 — Verbosity boundary: log at every level, filter at each threshold
// ===========================================================================

TEST_F(TelemetryLoggerTest, VerbosityFilter_InfoPassesAtInfoThreshold)
{
    logger.setVerbosity(VL::INFO);
    logger.log(TID::CONTROL, "info-msg", VL::INFO, ms(1));
    logger.drain(captureWrite, &output);
    EXPECT_NE(output.find("info-msg"), std::string::npos);
}

TEST_F(TelemetryLoggerTest, VerbosityFilter_StatusPassesAtInfoThreshold)
{
    logger.setVerbosity(VL::INFO);
    logger.log(TID::CONTROL, "status-msg", VL::STATUS, ms(1));
    logger.drain(captureWrite, &output);
    EXPECT_NE(output.find("status-msg"), std::string::npos);
}

// ===========================================================================
// v0.2.0 — Output format: timestamp in milliseconds
// ===========================================================================

TEST_F(TelemetryLoggerTest, OutputFormat_ContainsMillisecondTimestamp)
{
    // 5000 µs → should appear as "5" ms in the output.
    logger.log(TID::CONTROL, "ts-test", VL::STATUS, ms(5));
    logger.drain(captureWrite, &output);
    EXPECT_NE(output.find("5"), std::string::npos);
}
