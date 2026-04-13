/**
 * @file test_SchedulerHealthMetrics.cpp
 * @brief Unit tests for Kernel::SchedulerHealthMetrics.
 *
 * Validates gap recording, max gap tracking, aggregate setting, and reset.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "sputteros/kernel/metrics/SchedulerHealthMetrics.h"
#include <gtest/gtest.h>

using namespace SputterOS;
using namespace SputterOS::Kernel;

// ===========================================================================
// Fixture
// ===========================================================================

class SchedulerHealthMetricsTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_metrics.reset();
        m_metrics.setMetricsWindowUs(0); // Disable windowing for basic tests
    }

    SchedulerHealthMetrics m_metrics;
};

// ===========================================================================
// Initial state
// ===========================================================================

TEST_F(SchedulerHealthMetricsTest, InitialState_AllZero)
{
    EXPECT_EQ(m_metrics.totalGapUs(), 0u);
    EXPECT_EQ(m_metrics.maxGapUs(), 0u);
    EXPECT_FLOAT_EQ(m_metrics.averageGapUs(), 0.0f);
    EXPECT_EQ(m_metrics.totalOverruns(), 0u);
    EXPECT_EQ(m_metrics.totalDeadlineMisses(), 0u);
    EXPECT_EQ(m_metrics.tickCount(), 0u);
}

// ===========================================================================
// Gap recording
// ===========================================================================

TEST_F(SchedulerHealthMetricsTest, RecordGap_SingleTick)
{
    m_metrics.recordGap(500);

    EXPECT_EQ(m_metrics.totalGapUs(), 500u);
    EXPECT_EQ(m_metrics.maxGapUs(), 500u);
    EXPECT_EQ(m_metrics.tickCount(), 1u);
    EXPECT_FLOAT_EQ(m_metrics.averageGapUs(), 500.0f);
}

TEST_F(SchedulerHealthMetricsTest, RecordGap_MultiTick_TracksPeak)
{
    m_metrics.recordGap(100);
    m_metrics.recordGap(500);
    m_metrics.recordGap(200);

    EXPECT_EQ(m_metrics.totalGapUs(), 800u);
    EXPECT_EQ(m_metrics.maxGapUs(), 500u);
    EXPECT_EQ(m_metrics.tickCount(), 3u);
    // Average: 800/3 ≈ 266.67
    EXPECT_NEAR(m_metrics.averageGapUs(), 266.67f, 0.1f);
}

// ===========================================================================
// Aggregate overruns / misses
// ===========================================================================

TEST_F(SchedulerHealthMetricsTest, SetAggregates)
{
    m_metrics.setAggregates(10, 3);

    EXPECT_EQ(m_metrics.totalOverruns(), 10u);
    EXPECT_EQ(m_metrics.totalDeadlineMisses(), 3u);
}

TEST_F(SchedulerHealthMetricsTest, SetAggregates_Overwrites)
{
    m_metrics.setAggregates(10, 3);
    m_metrics.setAggregates(20, 5);

    EXPECT_EQ(m_metrics.totalOverruns(), 20u);
    EXPECT_EQ(m_metrics.totalDeadlineMisses(), 5u);
}

// ===========================================================================
// Reset
// ===========================================================================

TEST_F(SchedulerHealthMetricsTest, Reset_ClearsAll)
{
    m_metrics.recordGap(500);
    m_metrics.recordGap(300);
    m_metrics.setAggregates(5, 2);

    m_metrics.reset();

    EXPECT_EQ(m_metrics.totalGapUs(), 0u);
    EXPECT_EQ(m_metrics.maxGapUs(), 0u);
    EXPECT_FLOAT_EQ(m_metrics.averageGapUs(), 0.0f);
    EXPECT_EQ(m_metrics.totalOverruns(), 0u);
    EXPECT_EQ(m_metrics.totalDeadlineMisses(), 0u);
    EXPECT_EQ(m_metrics.tickCount(), 0u);
}

// ===========================================================================
// Rolling window — time-based
// ===========================================================================

class SchedulerHealthWindowTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_metrics.reset();
        m_metrics.setMetricsWindowUs(1'000'000); // 1 s window
    }

    SchedulerHealthMetrics m_metrics;
};

TEST_F(SchedulerHealthWindowTest, BeforeRotation_ReturnsInProgress)
{
    m_metrics.recordGap(500, 100);
    m_metrics.recordGap(200, 200);

    EXPECT_EQ(m_metrics.totalGapUs(), 700u);
    EXPECT_EQ(m_metrics.maxGapUs(), 500u);
    EXPECT_EQ(m_metrics.tickCount(), 2u);
}

TEST_F(SchedulerHealthWindowTest, Rotation_SnapshotsToReported)
{
    m_metrics.recordGap(500, 100);
    m_metrics.recordGap(200, 200);

    // Trigger rotation (10 µs gap included in old window)
    m_metrics.recordGap(10, 1'100'000);

    // Reported window: gaps 500 + 200 + 10 = 710, 3 ticks
    EXPECT_EQ(m_metrics.totalGapUs(), 710u);
    EXPECT_EQ(m_metrics.maxGapUs(), 500u);
    EXPECT_EQ(m_metrics.tickCount(), 3u);
    EXPECT_NEAR(m_metrics.averageGapUs(), 236.67f, 0.1f);
}

TEST_F(SchedulerHealthWindowTest, SecondRotation_OverwritesReported)
{
    m_metrics.recordGap(500, 100);

    // Trigger first rotation (800 gap included in window 1)
    m_metrics.recordGap(800, 1'100'000);
    // Window 1 reported: total=1300, max=800, count=2

    // Window 2 mid-window sample
    m_metrics.recordGap(50, 1'200'000);

    // Trigger second rotation (10 gap included in window 2)
    m_metrics.recordGap(10, 2'200'000);

    // Window 2 reported: 50 + 10 = 60, count=2
    EXPECT_EQ(m_metrics.totalGapUs(), 60u);
    EXPECT_EQ(m_metrics.maxGapUs(), 50u);
    EXPECT_EQ(m_metrics.tickCount(), 2u);
}

TEST_F(SchedulerHealthWindowTest, DisabledWindow_NeverRotates)
{
    m_metrics.setMetricsWindowUs(0);

    m_metrics.recordGap(500, 100);
    m_metrics.recordGap(200, 100'000'000);

    EXPECT_EQ(m_metrics.totalGapUs(), 700u);
    EXPECT_EQ(m_metrics.tickCount(), 2u);
}

TEST_F(SchedulerHealthWindowTest, Reset_ClearsWindowState)
{
    m_metrics.recordGap(500, 100);
    m_metrics.recordGap(10, 1'100'000); // trigger rotation

    m_metrics.reset();

    EXPECT_EQ(m_metrics.totalGapUs(), 0u);
    EXPECT_EQ(m_metrics.maxGapUs(), 0u);
    EXPECT_EQ(m_metrics.tickCount(), 0u);
}
