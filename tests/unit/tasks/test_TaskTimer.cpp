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
