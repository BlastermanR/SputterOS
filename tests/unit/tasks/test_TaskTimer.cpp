/**
 * @file test_TaskTimer.cpp
 * @brief Unit tests for Kernel::TaskTimer execution timing monitor.
 *
 * Validates start/stop duration measurement, max tracking, rolling
 * average, budget overage detection, and reset behaviour. Uses an
 * injectable lambda-style clock source for deterministic control.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "sputteros/kernel/TaskTimer.h"
#include <gtest/gtest.h>
#include <limits>

using namespace SputterOS;
using namespace SputterOS::Kernel;

// ===========================================================================
// Controllable clock source
// ===========================================================================

namespace
{

static uint64_t s_timerClock = 0;
uint64_t        timerClock() { return s_timerClock; }

} // namespace

// ===========================================================================
// Fixture
// ===========================================================================

class TaskTimerTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        s_timerClock = 0;
        m_timer.setClockSource(timerClock);
        m_timer.reset();
    }

    TaskTimer m_timer;
};

// ===========================================================================
// Initial state
// ===========================================================================

TEST_F(TaskTimerTest, InitialState_AllZero)
{
    EXPECT_EQ(m_timer.lastDuration(), 0u);
    EXPECT_EQ(m_timer.maxDuration(), 0u);
    EXPECT_FLOAT_EQ(m_timer.getAverageDurationUs(), 0.0f);
    EXPECT_EQ(m_timer.sampleCount(), 0u);
}

// ===========================================================================
// start / stop cycle
// ===========================================================================

TEST_F(TaskTimerTest, StartStop_MeasuresDuration)
{
    s_timerClock = 1000;
    m_timer.start();

    s_timerClock = 1500;
    m_timer.stop();

    EXPECT_EQ(m_timer.lastDuration(), 500u);
    EXPECT_EQ(m_timer.sampleCount(), 1u);
}

TEST_F(TaskTimerTest, StartStop_ZeroDuration)
{
    s_timerClock = 5000;
    m_timer.start();
    m_timer.stop(); // same clock → 0 elapsed

    EXPECT_EQ(m_timer.lastDuration(), 0u);
    EXPECT_EQ(m_timer.sampleCount(), 1u);
}

// ===========================================================================
// maxDuration tracking
// ===========================================================================

TEST_F(TaskTimerTest, MaxDuration_TracksPeak)
{
    // First cycle: 100 µs.
    s_timerClock = 1000;
    m_timer.start();
    s_timerClock = 1100;
    m_timer.stop();
    EXPECT_EQ(m_timer.maxDuration(), 100u);

    // Second cycle: 300 µs (new peak).
    s_timerClock = 2000;
    m_timer.start();
    s_timerClock = 2300;
    m_timer.stop();
    EXPECT_EQ(m_timer.maxDuration(), 300u);

    // Third cycle: 50 µs (max stays at 300).
    s_timerClock = 3000;
    m_timer.start();
    s_timerClock = 3050;
    m_timer.stop();
    EXPECT_EQ(m_timer.maxDuration(), 300u);
}

// ===========================================================================
// getAverageDurationUs
// ===========================================================================

TEST_F(TaskTimerTest, Average_ZeroSamples_ReturnsZero) { EXPECT_FLOAT_EQ(m_timer.getAverageDurationUs(), 0.0f); }

TEST_F(TaskTimerTest, Average_SingleSample)
{
    s_timerClock = 0;
    m_timer.start();
    s_timerClock = 200;
    m_timer.stop();

    EXPECT_FLOAT_EQ(m_timer.getAverageDurationUs(), 200.0f);
}

TEST_F(TaskTimerTest, Average_MultipleSamples)
{
    // Sample 1: 100 µs.
    s_timerClock = 0;
    m_timer.start();
    s_timerClock = 100;
    m_timer.stop();

    // Sample 2: 300 µs.
    s_timerClock = 1000;
    m_timer.start();
    s_timerClock = 1300;
    m_timer.stop();

    // Average = (100 + 300) / 2 = 200.
    EXPECT_FLOAT_EQ(m_timer.getAverageDurationUs(), 200.0f);
    EXPECT_EQ(m_timer.sampleCount(), 2u);
}

// ===========================================================================
// isOverBudget
// ===========================================================================

TEST_F(TaskTimerTest, IsOverBudget_UnderBudget_ReturnsFalse)
{
    s_timerClock = 0;
    m_timer.start();
    s_timerClock = 100;
    m_timer.stop();

    EXPECT_FALSE(m_timer.isOverBudget(200));
}

TEST_F(TaskTimerTest, IsOverBudget_ExactlyAtBudget_ReturnsFalse)
{
    s_timerClock = 0;
    m_timer.start();
    s_timerClock = 200;
    m_timer.stop();

    EXPECT_FALSE(m_timer.isOverBudget(200)); // equal, not over
}

TEST_F(TaskTimerTest, IsOverBudget_OverBudget_ReturnsTrue)
{
    s_timerClock = 0;
    m_timer.start();
    s_timerClock = 201;
    m_timer.stop();

    EXPECT_TRUE(m_timer.isOverBudget(200));
}

// ===========================================================================
// reset
// ===========================================================================

TEST_F(TaskTimerTest, Reset_ClearsAllStatistics)
{
    // Accumulate some data.
    s_timerClock = 0;
    m_timer.start();
    s_timerClock = 500;
    m_timer.stop();

    m_timer.reset();

    EXPECT_EQ(m_timer.lastDuration(), 0u);
    EXPECT_EQ(m_timer.maxDuration(), 0u);
    EXPECT_FLOAT_EQ(m_timer.getAverageDurationUs(), 0.0f);
    EXPECT_EQ(m_timer.sampleCount(), 0u);
}

// ===========================================================================
// overrunCount / deadlineMissCount
// ===========================================================================

TEST_F(TaskTimerTest, OverrunCounter_InitiallyZero) { EXPECT_EQ(m_timer.overrunCount(), 0u); }

TEST_F(TaskTimerTest, OverrunCounter_IncrementsOnRecord)
{
    m_timer.recordOverrun();
    EXPECT_EQ(m_timer.overrunCount(), 1u);

    m_timer.recordOverrun();
    m_timer.recordOverrun();
    EXPECT_EQ(m_timer.overrunCount(), 3u);
}

TEST_F(TaskTimerTest, DeadlineMissCounter_InitiallyZero) { EXPECT_EQ(m_timer.deadlineMissCount(), 0u); }

TEST_F(TaskTimerTest, DeadlineMissCounter_IncrementsOnRecord)
{
    m_timer.recordDeadlineMiss();
    EXPECT_EQ(m_timer.deadlineMissCount(), 1u);

    m_timer.recordDeadlineMiss();
    EXPECT_EQ(m_timer.deadlineMissCount(), 2u);
}

TEST_F(TaskTimerTest, Reset_ClearsOverrunAndDeadlineMissCounters)
{
    m_timer.recordOverrun();
    m_timer.recordOverrun();
    m_timer.recordDeadlineMiss();

    m_timer.reset();

    EXPECT_EQ(m_timer.overrunCount(), 0u);
    EXPECT_EQ(m_timer.deadlineMissCount(), 0u);
}

TEST_F(TaskTimerTest, Reset_AllowsCleanReuseAfterReset)
{
    // First session.
    s_timerClock = 0;
    m_timer.start();
    s_timerClock = 1000;
    m_timer.stop();

    m_timer.reset();

    // Second session after reset.
    s_timerClock = 5000;
    m_timer.start();
    s_timerClock = 5050;
    m_timer.stop();

    EXPECT_EQ(m_timer.lastDuration(), 50u);
    EXPECT_EQ(m_timer.maxDuration(), 50u);
    EXPECT_EQ(m_timer.sampleCount(), 1u);
}

// ===========================================================================
// No clock source
// ===========================================================================

TEST_F(TaskTimerTest, NoClockSource_DurationIsZero)
{
    TaskTimer unclockedTimer;
    unclockedTimer.reset();

    unclockedTimer.start();
    unclockedTimer.stop();

    EXPECT_EQ(unclockedTimer.lastDuration(), 0u);
    EXPECT_EQ(unclockedTimer.sampleCount(), 1u);
}

// ===========================================================================
// minDuration tracking
// ===========================================================================

TEST_F(TaskTimerTest, MinDuration_InitiallyMaxAfterReset)
{
    // After reset, minDuration should be sentinel max value
    EXPECT_EQ(m_timer.minDuration(), std::numeric_limits<SputterMicros>::max());
}

TEST_F(TaskTimerTest, MinDuration_TracksSmallest)
{
    // First cycle: 300 µs
    s_timerClock = 1000;
    m_timer.start();
    s_timerClock = 1300;
    m_timer.stop();
    EXPECT_EQ(m_timer.minDuration(), 300u);

    // Second cycle: 100 µs (new min)
    s_timerClock = 2000;
    m_timer.start();
    s_timerClock = 2100;
    m_timer.stop();
    EXPECT_EQ(m_timer.minDuration(), 100u);

    // Third cycle: 500 µs (min stays at 100)
    s_timerClock = 3000;
    m_timer.start();
    s_timerClock = 3500;
    m_timer.stop();
    EXPECT_EQ(m_timer.minDuration(), 100u);
}

TEST_F(TaskTimerTest, MinDuration_ResetClearsTracking)
{
    s_timerClock = 0;
    m_timer.start();
    s_timerClock = 50;
    m_timer.stop();
    EXPECT_EQ(m_timer.minDuration(), 50u);

    m_timer.reset();
    EXPECT_EQ(m_timer.minDuration(), std::numeric_limits<SputterMicros>::max());
}

// ===========================================================================
// Histogram tracking
// ===========================================================================

TEST_F(TaskTimerTest, Histogram_InitiallyAllZero)
{
    const uint32_t *hist = m_timer.histogram();
    for (std::size_t i = 0; i < TaskTimer::kHistogramBuckets; ++i)
    {
        EXPECT_EQ(hist[i], 0u) << "Bucket " << i << " should be zero after reset";
    }
}

TEST_F(TaskTimerTest, Histogram_SingleSample_CorrectBucket)
{
    // 200 µs → bucket 0 (range [0, 500))
    s_timerClock = 0;
    m_timer.start();
    s_timerClock = 200;
    m_timer.stop();

    const uint32_t *hist = m_timer.histogram();
    EXPECT_EQ(hist[0], 1u);
    for (std::size_t i = 1; i < TaskTimer::kHistogramBuckets; ++i)
    {
        EXPECT_EQ(hist[i], 0u);
    }
}

TEST_F(TaskTimerTest, Histogram_MultipleBuckets)
{
    // 200 µs → bucket 0 [0, 500)
    s_timerClock = 0;
    m_timer.start();
    s_timerClock = 200;
    m_timer.stop();

    // 700 µs → bucket 1 [500, 1000)
    s_timerClock = 1000;
    m_timer.start();
    s_timerClock = 1700;
    m_timer.stop();

    // 1200 µs → bucket 2 [1000, 1500)
    s_timerClock = 2000;
    m_timer.start();
    s_timerClock = 3200;
    m_timer.stop();

    const uint32_t *hist = m_timer.histogram();
    EXPECT_EQ(hist[0], 1u);
    EXPECT_EQ(hist[1], 1u);
    EXPECT_EQ(hist[2], 1u);
}

TEST_F(TaskTimerTest, Histogram_OverflowClampedToLastBucket)
{
    // Very large duration: 100000 µs → should clamp to last bucket
    s_timerClock = 0;
    m_timer.start();
    s_timerClock = 100000;
    m_timer.stop();

    const uint32_t *hist = m_timer.histogram();
    EXPECT_EQ(hist[TaskTimer::kHistogramBuckets - 1], 1u);
    for (std::size_t i = 0; i < TaskTimer::kHistogramBuckets - 1; ++i)
    {
        EXPECT_EQ(hist[i], 0u);
    }
}

TEST_F(TaskTimerTest, Histogram_BucketBoundary)
{
    // Exactly at bucket boundary: 500 µs → bucket 1 [500, 1000)
    s_timerClock = 0;
    m_timer.start();
    s_timerClock = 500;
    m_timer.stop();

    const uint32_t *hist = m_timer.histogram();
    EXPECT_EQ(hist[1], 1u);
    EXPECT_EQ(hist[0], 0u);
}

TEST_F(TaskTimerTest, Histogram_ResetClearsAll)
{
    s_timerClock = 0;
    m_timer.start();
    s_timerClock = 200;
    m_timer.stop();

    s_timerClock = 1000;
    m_timer.start();
    s_timerClock = 1700;
    m_timer.stop();

    m_timer.reset();

    const uint32_t *hist = m_timer.histogram();
    for (std::size_t i = 0; i < TaskTimer::kHistogramBuckets; ++i)
    {
        EXPECT_EQ(hist[i], 0u);
    }
}

TEST_F(TaskTimerTest, Histogram_Constants)
{
    EXPECT_EQ(TaskTimer::histogramBucketCount(), 8u);
    EXPECT_EQ(TaskTimer::histogramBucketWidthUs(), 500u);
}

// ===========================================================================
// percentileUs
// ===========================================================================

TEST_F(TaskTimerTest, Percentile_NoSamples_ReturnsZero) { EXPECT_EQ(m_timer.percentileUs(0.5f), 0u); }

TEST_F(TaskTimerTest, Percentile_ZeroPercentile_ReturnsZero)
{
    s_timerClock = 0;
    m_timer.start();
    s_timerClock = 200;
    m_timer.stop();

    EXPECT_EQ(m_timer.percentileUs(0.0f), 0u);
}

TEST_F(TaskTimerTest, Percentile_FullPercentile_ReturnsMax)
{
    s_timerClock = 0;
    m_timer.start();
    s_timerClock = 200;
    m_timer.stop();

    EXPECT_EQ(m_timer.percentileUs(1.0f), 200u);
}

TEST_F(TaskTimerTest, Percentile_AllSamplesInOneBucket)
{
    // 10 samples, all in bucket 0 [0, 500)
    for (int i = 0; i < 10; ++i)
    {
        s_timerClock = static_cast<uint64_t>(i) * 1000;
        m_timer.start();
        s_timerClock += 100;
        m_timer.stop();
    }

    // p50 should be within bucket 0
    SputterMicros p50 = m_timer.percentileUs(0.5f);
    EXPECT_GE(p50, 0u);
    EXPECT_LT(p50, 500u);
}

TEST_F(TaskTimerTest, Percentile_SpreadAcrossBuckets)
{
    // 5 samples in bucket 0 [0, 500): durations of 200 µs each
    for (int i = 0; i < 5; ++i)
    {
        s_timerClock = static_cast<uint64_t>(i) * 2000;
        m_timer.start();
        s_timerClock += 200;
        m_timer.stop();
    }

    // 5 samples in bucket 2 [1000, 1500): durations of 1200 µs each
    for (int i = 0; i < 5; ++i)
    {
        s_timerClock = 20000 + static_cast<uint64_t>(i) * 3000;
        m_timer.start();
        s_timerClock += 1200;
        m_timer.stop();
    }

    // p50 — 50th percentile is at the boundary between the two groups
    SputterMicros p50 = m_timer.percentileUs(0.5f);
    // First 5 samples in bucket 0, next 5 in bucket 2
    // At p50 (rank 5), cumulative in bucket 0 = 5, so p50 should be at top of bucket 0
    EXPECT_LE(p50, 500u);

    // p90 — should be well into bucket 2
    SputterMicros p90 = m_timer.percentileUs(0.9f);
    EXPECT_GE(p90, 1000u);
}
