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
    void SetUp() override { m_mon.reset(); }

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
