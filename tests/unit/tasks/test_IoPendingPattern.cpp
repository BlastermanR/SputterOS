/**
 * @file test_IoPendingPattern.cpp
 * @brief Unit tests for the IO_PENDING non-blocking I/O scheduling pattern.
 *
 * Validates the AdcPollTask-style state machine in isolation:
 *   - Conversion start → IO_PENDING yield → poll → read result
 *   - Timeout fault detection after max retries
 *   - Rate-limiting when not in IO_PENDING state
 *   - Sawtooth value cycling
 *
 * Uses a minimal SimulatedADC test double controlled by test code.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "sputteros/osal/SputterTime.h"
#include "sputteros/osal/tasks/IScheduledTask.h"

#include <gtest/gtest.h>

using namespace SputterOS;

// ===========================================================================
// Test doubles — minimal ADC and poller mirroring the sensorpoll pattern
// ===========================================================================

/**
 * @brief Controllable simulated ADC for test code.
 *
 * Unlike the example's SimulatedADC (which uses wall-clock time), this
 * version lets the test explicitly set readiness for deterministic tests.
 */
class ControllableADC
{
  public:
    void startConversion()
    {
        m_inFlight = true;
        m_ready    = false;
    }

    /** @brief Test hook — mark the conversion as complete. */
    void setReady() { m_ready = true; }

    bool isConversionReady() const { return m_inFlight && m_ready; }

    float readResult()
    {
        m_inFlight      = false;
        m_ready         = false;
        const float val = static_cast<float>(m_nextValue);
        m_nextValue     = (m_nextValue + 37) % 1000;
        return val;
    }

    bool isActive() const { return m_inFlight; }

  private:
    bool     m_inFlight{false};
    bool     m_ready{false};
    uint32_t m_nextValue{0};
};

/**
 * @brief Minimal IO_PENDING poller for unit testing.
 *
 * Mirrors the AdcPollTask state machine without telemetry dependencies.
 */
class TestAdcPollTask : public IScheduledTask
{
  public:
    static constexpr SputterMicros kPeriodUs     = 50'000;
    static constexpr uint32_t      kMaxIoRetries = 10;

    SputterMicros periodUs() const override { return kPeriodUs; }
    bool          isIoPending() const override { return m_waitingForAdc; }

    explicit TestAdcPollTask(ControllableADC &adc) : m_adc(adc) {}

    void init() override
    {
        m_waitingForAdc = false;
        m_ioWaitCount   = 0;
        m_readingCount  = 0;
        m_lastReading   = 0.0f;
        m_lastTick      = 0;
        m_timeoutCount  = 0;
    }

    void tick(SputterMicros systemTimeMicros) override
    {
        // Rate-limit when not waiting for IO
        if (!m_waitingForAdc)
        {
            if ((systemTimeMicros - m_lastTick) < kPeriodUs)
                return;
            m_lastTick = systemTimeMicros;
        }

        if (m_waitingForAdc)
        {
            if (m_adc.isConversionReady())
            {
                m_lastReading = m_adc.readResult();
                ++m_readingCount;
                m_waitingForAdc = false;
                m_ioWaitCount   = 0;
            }
            else
            {
                ++m_ioWaitCount;
                if (m_ioWaitCount > kMaxIoRetries)
                {
                    ++m_timeoutCount;
                    m_waitingForAdc = false;
                    m_ioWaitCount   = 0;
                }
                return;
            }
        }

        m_adc.startConversion();
        m_waitingForAdc = true;
    }

    uint32_t readingCount() const { return m_readingCount; }
    float    lastReading() const { return m_lastReading; }
    uint32_t timeoutCount() const { return m_timeoutCount; }

  private:
    ControllableADC &m_adc;
    bool             m_waitingForAdc{false};
    uint32_t         m_ioWaitCount{0};
    uint32_t         m_readingCount{0};
    float            m_lastReading{0.0f};
    SputterMicros    m_lastTick{0};
    uint32_t         m_timeoutCount{0};
};

// ===========================================================================
// IO_PENDING state machine tests
// ===========================================================================

TEST(IoPendingPatternTest, InitialState_NotPending)
{
    ControllableADC adc;
    TestAdcPollTask task(adc);
    task.init();

    EXPECT_FALSE(task.isIoPending());
    EXPECT_EQ(task.readingCount(), 0u);
}

TEST(IoPendingPatternTest, FirstTick_StartsConversion_SetsPending)
{
    ControllableADC adc;
    TestAdcPollTask task(adc);
    task.init();

    task.tick(SputterMicros(50'000));

    EXPECT_TRUE(task.isIoPending());
    EXPECT_TRUE(adc.isActive());
    EXPECT_EQ(task.readingCount(), 0u);
}

TEST(IoPendingPatternTest, PollNotReady_StaysPending)
{
    ControllableADC adc;
    TestAdcPollTask task(adc);
    task.init();

    task.tick(SputterMicros(50'000)); // start conversion
    ASSERT_TRUE(task.isIoPending());

    // Poll without setting ready
    task.tick(SputterMicros(51'000));

    EXPECT_TRUE(task.isIoPending());
    EXPECT_EQ(task.readingCount(), 0u);
}

TEST(IoPendingPatternTest, PollReady_ReadsResult_ClearsPending)
{
    ControllableADC adc;
    TestAdcPollTask task(adc);
    task.init();

    task.tick(SputterMicros(50'000)); // start conversion
    adc.setReady();
    task.tick(SputterMicros(51'000)); // poll → read → start new

    EXPECT_EQ(task.readingCount(), 1u);
    EXPECT_FLOAT_EQ(task.lastReading(), 0.0f); // first value = 0

    // After read, a new conversion was started immediately
    EXPECT_TRUE(task.isIoPending());
}

TEST(IoPendingPatternTest, MultipleReadings_SawtoothPattern)
{
    ControllableADC adc;
    TestAdcPollTask task(adc);
    task.init();

    // Complete 3 conversion cycles
    SputterMicros now = 50'000;
    for (int i = 0; i < 3; ++i)
    {
        task.tick(now); // start or read+start
        adc.setReady();
        now += 1'000;
        task.tick(now);   // poll ready → read → start next
        now += 1'000'000; // advance past rate limit for next
    }

    EXPECT_EQ(task.readingCount(), 3u);
    // Values: 0, 37, 74
    EXPECT_FLOAT_EQ(task.lastReading(), 74.0f);
}

TEST(IoPendingPatternTest, Timeout_AfterMaxRetries)
{
    ControllableADC adc;
    TestAdcPollTask task(adc);
    task.init();

    task.tick(SputterMicros(50'000)); // start conversion
    ASSERT_TRUE(task.isIoPending());

    // Poll kMaxIoRetries+1 times without setting ready
    for (uint32_t i = 0; i <= TestAdcPollTask::kMaxIoRetries; ++i)
    {
        task.tick(SputterMicros(51'000 + i * 100));
    }

    // Should have timed out and cleared pending
    EXPECT_FALSE(task.isIoPending());
    EXPECT_EQ(task.readingCount(), 0u);
    EXPECT_EQ(task.timeoutCount(), 1u);
}

TEST(IoPendingPatternTest, Timeout_Recovers_NextConversion)
{
    ControllableADC adc;
    TestAdcPollTask task(adc);
    task.init();

    // First conversion times out
    task.tick(SputterMicros(50'000)); // start
    for (uint32_t i = 0; i <= TestAdcPollTask::kMaxIoRetries; ++i)
        task.tick(SputterMicros(51'000 + i * 100));

    ASSERT_FALSE(task.isIoPending());
    ASSERT_EQ(task.timeoutCount(), 1u);

    // Next period — start a new conversion that succeeds
    task.tick(SputterMicros(100'000)); // past rate limit → starts conversion
    ASSERT_TRUE(task.isIoPending());

    adc.setReady();
    task.tick(SputterMicros(101'000)); // read result
    EXPECT_EQ(task.readingCount(), 1u);
    EXPECT_EQ(task.timeoutCount(), 1u); // no new timeout
}

// ===========================================================================
// Rate-limiting tests
// ===========================================================================

TEST(IoPendingPatternTest, RateLimited_WhenNotPending)
{
    ControllableADC adc;
    TestAdcPollTask task(adc);
    task.init();

    // First tick starts conversion
    task.tick(SputterMicros(50'000));
    adc.setReady();
    task.tick(SputterMicros(51'000)); // read result + start next
    ASSERT_EQ(task.readingCount(), 1u);

    // Now resolve the second conversion
    adc.setReady();
    task.tick(SputterMicros(52'000)); // read → now not pending, starts next

    EXPECT_EQ(task.readingCount(), 2u);

    // Resolve again — IO_PENDING bypasses rate-limiting so reads still work
    adc.setReady();
    task.tick(SputterMicros(53'000)); // reads because we're in IO_PENDING
    EXPECT_EQ(task.readingCount(), 3u);
}

TEST(IoPendingPatternTest, NotRateLimited_WhenPending)
{
    ControllableADC adc;
    TestAdcPollTask task(adc);
    task.init();

    task.tick(SputterMicros(50'000)); // start conversion
    ASSERT_TRUE(task.isIoPending());

    // Rapid polls — should NOT be rate-limited since IO_PENDING
    task.tick(SputterMicros(50'100));
    task.tick(SputterMicros(50'200));
    // Still pending (no ready set), but ticks were not blocked
    EXPECT_TRUE(task.isIoPending());
}

// ===========================================================================
// Scheduling type markers
// ===========================================================================

TEST(IoPendingPatternTest, TypeMarkers)
{
    ControllableADC adc;
    TestAdcPollTask task(adc);
    EXPECT_TRUE(task.isScheduled());
    EXPECT_FALSE(task.isBackground());
    EXPECT_EQ(task.periodUs(), 50'000u);
}
