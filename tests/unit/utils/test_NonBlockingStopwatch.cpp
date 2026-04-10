/**
 * @file test_NonBlockingStopwatch.cpp
 * @brief Unit tests for NonBlockingStopwatch.
 *
 * Injects controlled timestamps to verify elapsed time, expiry detection,
 * and start/stop/reset semantics.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */

#include "sputteros/utils/NonBlockingStopwatch.h"
#include <gtest/gtest.h>

using namespace SputterOS;

/// Helper: microseconds from a millisecond-level value for readability.
static constexpr SputterMicros ms(uint64_t milliseconds) { return milliseconds * 1000u; }

// ===========================================================================
// Fixture
// ===========================================================================

class StopwatchTest : public ::testing::Test
{
  protected:
    NonBlockingStopwatch sw;
};

// ---------------------------------------------------------------------------
// Initial state
// ---------------------------------------------------------------------------

TEST_F(StopwatchTest, InitialState_NotRunning) { EXPECT_FALSE(sw.isRunning()); }

TEST_F(StopwatchTest, InitialState_ElapsedIsZero)
{
    EXPECT_EQ(sw.elapsed(ms(0)), ms(0));
    EXPECT_EQ(sw.elapsed(ms(1000)), ms(0));
}

TEST_F(StopwatchTest, InitialState_HasExpiredIsFalse) { EXPECT_FALSE(sw.hasExpired(ms(10000), ms(0))); }

// ---------------------------------------------------------------------------
// start / isRunning / elapsed
// ---------------------------------------------------------------------------

TEST_F(StopwatchTest, Start_SetsRunning)
{
    sw.start(ms(0));
    EXPECT_TRUE(sw.isRunning());
}

TEST_F(StopwatchTest, Elapsed_ReturnsTimeSinceStart)
{
    sw.start(ms(1000));
    EXPECT_EQ(sw.elapsed(ms(1500)), ms(500));
}

TEST_F(StopwatchTest, Elapsed_AtStartTime_IsZero)
{
    sw.start(ms(500));
    EXPECT_EQ(sw.elapsed(ms(500)), ms(0));
}

// ---------------------------------------------------------------------------
// stop
// ---------------------------------------------------------------------------

TEST_F(StopwatchTest, Stop_ClearsRunning)
{
    sw.start(ms(0));
    sw.stop();
    EXPECT_FALSE(sw.isRunning());
}

TEST_F(StopwatchTest, Elapsed_AfterStop_IsZero)
{
    sw.start(ms(0));
    sw.stop();
    EXPECT_EQ(sw.elapsed(ms(5000)), ms(0));
}

// ---------------------------------------------------------------------------
// hasExpired
// ---------------------------------------------------------------------------

TEST_F(StopwatchTest, HasExpired_BeforeDuration_ReturnsFalse)
{
    sw.start(ms(0));
    EXPECT_FALSE(sw.hasExpired(ms(500), ms(1000)));
}

TEST_F(StopwatchTest, HasExpired_ExactlyAtDuration_ReturnsTrue)
{
    sw.start(ms(0));
    EXPECT_TRUE(sw.hasExpired(ms(1000), ms(1000)));
}

TEST_F(StopwatchTest, HasExpired_AfterDuration_ReturnsTrue)
{
    sw.start(ms(0));
    EXPECT_TRUE(sw.hasExpired(ms(2000), ms(1000)));
}

TEST_F(StopwatchTest, HasExpired_WhenStopped_ReturnsFalse)
{
    sw.start(ms(0));
    sw.stop();
    EXPECT_FALSE(sw.hasExpired(ms(5000), ms(100)));
}

// ---------------------------------------------------------------------------
// restart: effectively re-arms relative to the new time
// ---------------------------------------------------------------------------

TEST_F(StopwatchTest, Start_CalledTwice_RestartsElapsed)
{
    sw.start(ms(0));
    sw.start(ms(1000)); // restart
    EXPECT_EQ(sw.elapsed(ms(1200)), ms(200));
}

// ---------------------------------------------------------------------------
// reset
// ---------------------------------------------------------------------------

TEST_F(StopwatchTest, Reset_StopsAndClearsState)
{
    sw.start(ms(0));
    sw.reset();
    EXPECT_FALSE(sw.isRunning());
    EXPECT_EQ(sw.elapsed(ms(9999)), ms(0));
}

// ---------------------------------------------------------------------------
// Large elapsed values (chrono int64_t range)
// ---------------------------------------------------------------------------

TEST_F(StopwatchTest, LargeElapsed_HandledCorrectly)
{
    // Start near a large value, verify elapsed is computed correctly.
    constexpr auto kStart = ms(1'000'000'000);
    constexpr auto kNow   = ms(1'000'000'511);
    sw.start(kStart);
    EXPECT_EQ(sw.elapsed(kNow), ms(511));
}

TEST_F(StopwatchTest, LargeElapsed_HasExpiredCorrect)
{
    constexpr auto kStart    = ms(1'000'000'000);
    constexpr auto kDuration = ms(512);
    constexpr auto kNow      = ms(1'000'000'512);
    sw.start(kStart);
    EXPECT_TRUE(sw.hasExpired(kNow, kDuration));
}
