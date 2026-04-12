/**
 * @file test_CoreUtilizationTracker.cpp
 * @brief Unit tests for Kernel::CoreUtilizationTracker.
 *
 * Validates utilization calculation, windowed accumulation, auto-reset
 * behaviour, and edge cases (zero ticks, zero wall time).
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "sputteros/kernel/metrics/CoreUtilizationTracker.h"
#include <gtest/gtest.h>

using namespace SputterOS;
using namespace SputterOS::Kernel;

// ===========================================================================
// Fixture
// ===========================================================================

class CoreUtilizationTrackerTest : public ::testing::Test
{
  protected:
    void SetUp() override { m_tracker.reset(); }

    CoreUtilizationTracker m_tracker;
};

// ===========================================================================
// Initial state
// ===========================================================================

TEST_F(CoreUtilizationTrackerTest, InitialState_ZeroUtilization)
{
    EXPECT_FLOAT_EQ(m_tracker.getUtilization(), 0.0f);
    EXPECT_EQ(m_tracker.getWindowUs(), 0u);
    EXPECT_EQ(m_tracker.getBusyUs(), 0u);
    EXPECT_EQ(m_tracker.getTickCount(), 0u);
}

// ===========================================================================
// Single tick
// ===========================================================================

TEST_F(CoreUtilizationTrackerTest, SingleTick_HalfBusy)
{
    m_tracker.recordTickStart(1000);
    m_tracker.recordTickEnd(2000, 500); // 500 µs busy out of 1000 µs wall

    EXPECT_FLOAT_EQ(m_tracker.getUtilization(), 0.5f);
    EXPECT_EQ(m_tracker.getWindowUs(), 1000u);
    EXPECT_EQ(m_tracker.getBusyUs(), 500u);
    EXPECT_EQ(m_tracker.getTickCount(), 1u);
}

TEST_F(CoreUtilizationTrackerTest, SingleTick_FullyBusy)
{
    m_tracker.recordTickStart(0);
    m_tracker.recordTickEnd(1000, 1000);

    EXPECT_FLOAT_EQ(m_tracker.getUtilization(), 1.0f);
}

TEST_F(CoreUtilizationTrackerTest, SingleTick_ZeroBusy)
{
    m_tracker.recordTickStart(0);
    m_tracker.recordTickEnd(1000, 0);

    EXPECT_FLOAT_EQ(m_tracker.getUtilization(), 0.0f);
}

// ===========================================================================
// Multiple ticks accumulate
// ===========================================================================

TEST_F(CoreUtilizationTrackerTest, MultipleTicks_Accumulation)
{
    // Tick 1: 200 µs busy / 1000 µs wall
    m_tracker.recordTickStart(0);
    m_tracker.recordTickEnd(1000, 200);

    // Tick 2: 800 µs busy / 1000 µs wall
    m_tracker.recordTickStart(1000);
    m_tracker.recordTickEnd(2000, 800);

    // Total: 1000 busy / 2000 wall = 0.5
    EXPECT_FLOAT_EQ(m_tracker.getUtilization(), 0.5f);
    EXPECT_EQ(m_tracker.getTickCount(), 2u);
}

// ===========================================================================
// Window auto-reset
// ===========================================================================

TEST_F(CoreUtilizationTrackerTest, WindowAutoReset_ReportsLastCompleteWindow)
{
    // Fill a full window of kWindowTicks ticks
    for (uint32_t i = 0; i < CoreUtilizationTracker::kWindowTicks; ++i)
    {
        SputterMicros start = static_cast<SputterMicros>(i) * 1000;
        m_tracker.recordTickStart(start);
        m_tracker.recordTickEnd(start + 1000, 250); // 25% utilization
    }

    // Now the window has rotated. The reported values should be from the complete window.
    EXPECT_FLOAT_EQ(m_tracker.getUtilization(), 0.25f);

    // After one more tick into the new window, reported values should still be the old window
    SputterMicros newStart = static_cast<SputterMicros>(CoreUtilizationTracker::kWindowTicks) * 1000;
    m_tracker.recordTickStart(newStart);
    m_tracker.recordTickEnd(newStart + 1000, 750); // 75% in new window

    // Should still report the complete window (25%), not the partial new one
    EXPECT_FLOAT_EQ(m_tracker.getUtilization(), 0.25f);
}

// ===========================================================================
// Reset
// ===========================================================================

TEST_F(CoreUtilizationTrackerTest, Reset_ClearsEverything)
{
    m_tracker.recordTickStart(0);
    m_tracker.recordTickEnd(1000, 500);

    m_tracker.reset();

    EXPECT_FLOAT_EQ(m_tracker.getUtilization(), 0.0f);
    EXPECT_EQ(m_tracker.getWindowUs(), 0u);
    EXPECT_EQ(m_tracker.getBusyUs(), 0u);
    EXPECT_EQ(m_tracker.getTickCount(), 0u);
}

// ===========================================================================
// Edge: rollover / zero wall time
// ===========================================================================

TEST_F(CoreUtilizationTrackerTest, ZeroWallTime_ZeroUtilization)
{
    m_tracker.recordTickStart(5000);
    m_tracker.recordTickEnd(5000, 0); // 0 wall time

    EXPECT_FLOAT_EQ(m_tracker.getUtilization(), 0.0f);
}
