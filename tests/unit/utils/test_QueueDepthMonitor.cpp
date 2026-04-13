/**
 * @file test_QueueDepthMonitor.cpp
 * @brief Unit tests for QueueDepthMonitor.
 *
 * Validates depth sampling, max tracking, average calculation, and reset.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "sputteros/utils/QueueDepthMonitor.h"
#include <gtest/gtest.h>

using namespace SputterOS;

// ===========================================================================
// Fixture
// ===========================================================================

class QueueDepthMonitorTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_mon.reset();
        m_mon.setMetricsWindowUs(0); // Disable windowing for basic tests
    }

    QueueDepthMonitor m_mon;
};

// ===========================================================================
// Initial state
// ===========================================================================

TEST_F(QueueDepthMonitorTest, InitialState_AllZero)
{
    EXPECT_EQ(m_mon.lastDepth(), 0u);
    EXPECT_EQ(m_mon.maxDepth(), 0u);
    EXPECT_FLOAT_EQ(m_mon.averageDepth(), 0.0f);
    EXPECT_EQ(m_mon.sampleCount(), 0u);
}

// ===========================================================================
// Single sample
// ===========================================================================

TEST_F(QueueDepthMonitorTest, SingleSample)
{
    m_mon.sample(5);

    EXPECT_EQ(m_mon.lastDepth(), 5u);
    EXPECT_EQ(m_mon.maxDepth(), 5u);
    EXPECT_FLOAT_EQ(m_mon.averageDepth(), 5.0f);
    EXPECT_EQ(m_mon.sampleCount(), 1u);
}

// ===========================================================================
// Max tracking
// ===========================================================================

TEST_F(QueueDepthMonitorTest, MaxDepth_TracksPeak)
{
    m_mon.sample(3);
    m_mon.sample(7);
    m_mon.sample(2);

    EXPECT_EQ(m_mon.maxDepth(), 7u);
    EXPECT_EQ(m_mon.lastDepth(), 2u);
}

// ===========================================================================
// Average
// ===========================================================================

TEST_F(QueueDepthMonitorTest, Average_MultiSamples)
{
    m_mon.sample(2);
    m_mon.sample(4);
    m_mon.sample(6);

    // (2 + 4 + 6) / 3 = 4.0
    EXPECT_FLOAT_EQ(m_mon.averageDepth(), 4.0f);
    EXPECT_EQ(m_mon.sampleCount(), 3u);
}

// ===========================================================================
// Reset
// ===========================================================================

TEST_F(QueueDepthMonitorTest, Reset_ClearsAll)
{
    m_mon.sample(10);
    m_mon.sample(20);

    m_mon.reset();

    EXPECT_EQ(m_mon.lastDepth(), 0u);
    EXPECT_EQ(m_mon.maxDepth(), 0u);
    EXPECT_FLOAT_EQ(m_mon.averageDepth(), 0.0f);
    EXPECT_EQ(m_mon.sampleCount(), 0u);
}

// ===========================================================================
// Zero depth samples
// ===========================================================================

TEST_F(QueueDepthMonitorTest, ZeroDepthSamples)
{
    m_mon.sample(0);
    m_mon.sample(0);

    EXPECT_EQ(m_mon.lastDepth(), 0u);
    EXPECT_EQ(m_mon.maxDepth(), 0u);
    EXPECT_FLOAT_EQ(m_mon.averageDepth(), 0.0f);
    EXPECT_EQ(m_mon.sampleCount(), 2u);
}

// ===========================================================================
// Rolling window — time-based
// ===========================================================================

class QueueDepthWindowTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_mon.reset();
        m_mon.setMetricsWindowUs(1'000'000); // 1 s window
    }

    QueueDepthMonitor m_mon;
};

TEST_F(QueueDepthWindowTest, BeforeRotation_ReturnsInProgress)
{
    m_mon.sample(3, 100);
    m_mon.sample(7, 200);

    EXPECT_EQ(m_mon.maxDepth(), 7u);
    EXPECT_FLOAT_EQ(m_mon.averageDepth(), 5.0f);
    EXPECT_EQ(m_mon.sampleCount(), 2u);
}

TEST_F(QueueDepthWindowTest, Rotation_SnapshotsToReported)
{
    m_mon.sample(3, 100);
    m_mon.sample(7, 200);

    // Trigger rotation (depth 1 included in old window)
    m_mon.sample(1, 1'100'000);

    // Reported window: samples 3, 7, 1 → max=7, avg=(3+7+1)/3=3.67, count=3
    EXPECT_EQ(m_mon.maxDepth(), 7u);
    EXPECT_NEAR(m_mon.averageDepth(), 3.67f, 0.01f);
    EXPECT_EQ(m_mon.sampleCount(), 3u);
    EXPECT_EQ(m_mon.lastDepth(), 1u); // lastDepth is always current
}

TEST_F(QueueDepthWindowTest, SecondRotation_OverwritesReported)
{
    m_mon.sample(3, 100);

    // Trigger first rotation (depth 10 included in window 1)
    m_mon.sample(10, 1'100'000);
    // Window 1 reported: max=10, count=2, avg=6.5

    // Window 2 mid-window sample
    m_mon.sample(5, 1'200'000);

    // Trigger second rotation (depth 1 included in window 2)
    m_mon.sample(1, 2'200'000);

    // Window 2 reported: samples 5, 1 → count=2, max=5, avg=3.0
    EXPECT_EQ(m_mon.maxDepth(), 5u);
    EXPECT_EQ(m_mon.sampleCount(), 2u);
    EXPECT_FLOAT_EQ(m_mon.averageDepth(), 3.0f);
}

TEST_F(QueueDepthWindowTest, DisabledWindow_NeverRotates)
{
    m_mon.setMetricsWindowUs(0);

    m_mon.sample(3, 100);
    m_mon.sample(7, 100'000'000);

    EXPECT_EQ(m_mon.maxDepth(), 7u);
    EXPECT_EQ(m_mon.sampleCount(), 2u);
}

TEST_F(QueueDepthWindowTest, Reset_ClearsWindowState)
{
    m_mon.sample(5, 100);
    m_mon.sample(1, 1'100'000); // trigger rotation

    m_mon.reset();

    EXPECT_EQ(m_mon.maxDepth(), 0u);
    EXPECT_EQ(m_mon.sampleCount(), 0u);
    EXPECT_FLOAT_EQ(m_mon.averageDepth(), 0.0f);
}
