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
    void SetUp() override { m_metrics.reset(); }

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
