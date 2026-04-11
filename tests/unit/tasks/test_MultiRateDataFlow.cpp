/**
 * @file test_MultiRateDataFlow.cpp
 * @brief Unit tests for multi-rate scheduling task data flow.
 *
 * Validates that FastSampleTask accumulates samples correctly,
 * SlowReportTask drains the accumulator, and IdleCounterTask
 * counts its dispatches.  These tests exercise the task logic
 * in isolation (no kernel, no SystemBuilder).
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "sputteros/osal/tasks/IBackgroundTask.h"
#include "sputteros/osal/tasks/IScheduledTask.h"
#include "sputteros/utils/logging/TelemetryLogger.h"

#include <gtest/gtest.h>

using namespace SputterOS;

// ===========================================================================
// Minimal test doubles — isolate task logic from example project headers
// ===========================================================================

/**
 * @brief Minimal fast-rate sampler for unit testing.
 *
 * Mirrors the accumulate-and-drain pattern of the multirate
 * example's FastSampleTask without depending on example headers.
 */
class TestFastSampleTask : public IScheduledTask
{
  public:
    static constexpr SputterMicros kPeriodUs = 10'000; // 10 ms = 100 Hz

    SputterMicros periodUs() const override { return kPeriodUs; }
    SputterMicros declaredWcetUs() const override { return 50; }

    void init() override
    {
        m_sampleCount = 0;
        m_accumulator = 0;
        m_lastTick    = 0;
    }

    void tick(SputterMicros systemTimeMicros) override
    {
        if ((systemTimeMicros - m_lastTick) < kPeriodUs)
            return;
        m_lastTick = systemTimeMicros;

        const uint32_t reading = m_sampleCount % 100;
        m_accumulator += reading;
        ++m_sampleCount;
    }

    uint32_t sampleCount() const { return m_sampleCount; }
    uint64_t accumulator() const { return m_accumulator; }

    uint64_t drainAccumulator()
    {
        const uint64_t val = m_accumulator;
        m_accumulator      = 0;
        return val;
    }

  private:
    uint32_t     m_sampleCount{0};
    uint64_t     m_accumulator{0};
    SputterMicros m_lastTick{0};
};

/**
 * @brief Minimal slow-rate reporter for unit testing.
 *
 * Drains data from a TestFastSampleTask on each tick.
 */
class TestSlowReportTask : public IScheduledTask
{
  public:
    static constexpr SputterMicros kPeriodUs = 500'000; // 500 ms = 2 Hz

    SputterMicros periodUs() const override { return kPeriodUs; }

    explicit TestSlowReportTask(TestFastSampleTask &sampler)
        : m_sampler(sampler), m_reportCount(0), m_lastDrainedAccum(0), m_lastTick(0)
    {
    }

    void init() override
    {
        m_reportCount      = 0;
        m_lastDrainedAccum = 0;
        m_lastTick         = 0;
    }

    void tick(SputterMicros systemTimeMicros) override
    {
        if ((systemTimeMicros - m_lastTick) < kPeriodUs)
            return;
        m_lastTick = systemTimeMicros;

        m_lastDrainedAccum = m_sampler.drainAccumulator();
        ++m_reportCount;
    }

    uint32_t reportCount() const { return m_reportCount; }
    uint64_t lastDrainedAccum() const { return m_lastDrainedAccum; }

  private:
    TestFastSampleTask &m_sampler;
    uint32_t            m_reportCount;
    uint64_t            m_lastDrainedAccum;
    SputterMicros       m_lastTick;
};

/**
 * @brief Minimal background task for unit testing.
 */
class TestIdleCounterTask : public IBackgroundTask
{
  public:
    SputterMicros maxBudgetUs() const override { return 1000; }

    void init() override { m_dispatchCount = 0; }

    void tick(SputterMicros /*systemTimeMicros*/) override { ++m_dispatchCount; }

    uint32_t dispatchCount() const { return m_dispatchCount; }

  private:
    uint32_t m_dispatchCount{0};
};

// ===========================================================================
// FastSampleTask tests
// ===========================================================================

TEST(MultiRateDataFlowTest, FastSample_InitResetsState)
{
    TestFastSampleTask task;
    task.tick(SputterMicros(10'000));
    task.tick(SputterMicros(20'000));
    ASSERT_GT(task.sampleCount(), 0u);

    task.init();
    EXPECT_EQ(task.sampleCount(), 0u);
    EXPECT_EQ(task.accumulator(), 0u);
}

TEST(MultiRateDataFlowTest, FastSample_AccumulatesReadings)
{
    TestFastSampleTask task;
    task.init();

    // Tick at 10 ms intervals — first fires at kPeriodUs
    for (uint32_t i = 1; i <= 5; ++i)
    {
        task.tick(SputterMicros(i * 10'000));
    }

    EXPECT_EQ(task.sampleCount(), 5u);
    // readings: 0, 1, 2, 3, 4 → sum = 10
    EXPECT_EQ(task.accumulator(), 10u);
}

TEST(MultiRateDataFlowTest, FastSample_RateLimited)
{
    TestFastSampleTask task;
    task.init();

    // First tick at kPeriodUs fires, then rapid ticks are rate-limited
    task.tick(SputterMicros(10'000));
    task.tick(SputterMicros(11'000));
    task.tick(SputterMicros(15'000));
    task.tick(SputterMicros(19'999));

    EXPECT_EQ(task.sampleCount(), 1u);
}

TEST(MultiRateDataFlowTest, FastSample_DrainReturnsAccumAndResets)
{
    TestFastSampleTask task;
    task.init();

    task.tick(SputterMicros(10'000));
    task.tick(SputterMicros(20'000));
    task.tick(SputterMicros(30'000));

    // readings: 0, 1, 2 → accum = 3
    EXPECT_EQ(task.accumulator(), 3u);
    const uint64_t drained = task.drainAccumulator();
    EXPECT_EQ(drained, 3u);
    EXPECT_EQ(task.accumulator(), 0u);
    // sampleCount should NOT reset
    EXPECT_EQ(task.sampleCount(), 3u);
}

TEST(MultiRateDataFlowTest, FastSample_SawtoothWrapsAt100)
{
    TestFastSampleTask task;
    task.init();

    // Run 101 periods starting at kPeriodUs
    for (uint32_t i = 1; i <= 101; ++i)
    {
        task.tick(SputterMicros(i * 10'000));
    }

    EXPECT_EQ(task.sampleCount(), 101u);

    // Sample 100 should be reading = 100 % 100 = 0 (wrapped)
    // Sum of 0..99 = 4950, then sample 100 adds 0 → 4950
    EXPECT_EQ(task.accumulator(), 4950u);
}

// ===========================================================================
// SlowReportTask tests
// ===========================================================================

TEST(MultiRateDataFlowTest, SlowReport_DrainsAccumulator)
{
    TestFastSampleTask fast;
    TestSlowReportTask slow(fast);
    fast.init();
    slow.init();

    // Run fast task for 50 samples (tick at kPeriodUs intervals)
    for (uint32_t i = 1; i <= 50; ++i)
    {
        fast.tick(SputterMicros(i * 10'000));
    }

    // Now tick the slow task at 500 ms
    slow.tick(SputterMicros(500'000));

    EXPECT_EQ(slow.reportCount(), 1u);
    // Sum of 0..49 = 1225
    EXPECT_EQ(slow.lastDrainedAccum(), 1225u);
    // Accumulator should be drained
    EXPECT_EQ(fast.accumulator(), 0u);
}

TEST(MultiRateDataFlowTest, SlowReport_RateLimited)
{
    TestFastSampleTask fast;
    TestSlowReportTask slow(fast);
    fast.init();
    slow.init();

    fast.tick(SputterMicros(10'000));
    slow.tick(SputterMicros(500'000));
    slow.tick(SputterMicros(600'000));
    slow.tick(SputterMicros(999'999));

    EXPECT_EQ(slow.reportCount(), 1u);
}

TEST(MultiRateDataFlowTest, SlowReport_MultipleDrains)
{
    TestFastSampleTask fast;
    TestSlowReportTask slow(fast);
    fast.init();
    slow.init();

    // First 500 ms: 50 fast samples, then drain
    for (uint32_t i = 1; i <= 50; ++i)
        fast.tick(SputterMicros(i * 10'000));
    slow.tick(SputterMicros(500'000));

    EXPECT_EQ(slow.reportCount(), 1u);
    EXPECT_EQ(slow.lastDrainedAccum(), 1225u);

    // Second 500 ms: 50 more samples, then drain
    for (uint32_t i = 51; i <= 100; ++i)
        fast.tick(SputterMicros(i * 10'000));
    slow.tick(SputterMicros(1'000'000));

    EXPECT_EQ(slow.reportCount(), 2u);
    // Readings 50..99: sum = 50+51+...+99 = 3725
    EXPECT_EQ(slow.lastDrainedAccum(), 3725u);
}

// ===========================================================================
// IdleCounterTask tests
// ===========================================================================

TEST(MultiRateDataFlowTest, IdleCounter_TypeMarkers)
{
    TestIdleCounterTask task;
    EXPECT_TRUE(task.isBackground());
    EXPECT_FALSE(task.isScheduled());
}

TEST(MultiRateDataFlowTest, IdleCounter_CountsDispatches)
{
    TestIdleCounterTask task;
    task.init();

    for (int i = 0; i < 100; ++i)
        task.tick(SputterMicros(i * 1000));

    EXPECT_EQ(task.dispatchCount(), 100u);
}

TEST(MultiRateDataFlowTest, IdleCounter_InitResets)
{
    TestIdleCounterTask task;
    task.init();
    task.tick(SputterMicros(0));
    task.tick(SputterMicros(1000));
    ASSERT_EQ(task.dispatchCount(), 2u);

    task.init();
    EXPECT_EQ(task.dispatchCount(), 0u);
}

TEST(MultiRateDataFlowTest, IdleCounter_BudgetValue)
{
    TestIdleCounterTask task;
    EXPECT_EQ(task.maxBudgetUs(), 1000u);
}

// ===========================================================================
// Scheduling type markers
// ===========================================================================

TEST(MultiRateDataFlowTest, FastSample_IsScheduled)
{
    TestFastSampleTask task;
    EXPECT_TRUE(task.isScheduled());
    EXPECT_FALSE(task.isBackground());
}

TEST(MultiRateDataFlowTest, SlowReport_IsScheduled)
{
    TestFastSampleTask fast;
    TestSlowReportTask slow(fast);
    EXPECT_TRUE(slow.isScheduled());
    EXPECT_FALSE(slow.isBackground());
}

TEST(MultiRateDataFlowTest, FastSample_PeriodAndWcet)
{
    TestFastSampleTask task;
    EXPECT_EQ(task.periodUs(), 10'000u);
    EXPECT_EQ(task.declaredWcetUs(), 50u);
}

TEST(MultiRateDataFlowTest, SlowReport_Period)
{
    TestFastSampleTask fast;
    TestSlowReportTask slow(fast);
    EXPECT_EQ(slow.periodUs(), 500'000u);
}
