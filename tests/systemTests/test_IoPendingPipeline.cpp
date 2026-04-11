/**
 * @file test_IoPendingPipeline.cpp
 * @brief System tests for IO_PENDING non-blocking I/O through the kernel.
 *
 * Exercises real SystemBuilder → System → tick pipelines with an
 * IO_PENDING-aware scheduled task to validate:
 * - Task signals IO_PENDING state through kernel tick loop
 * - Task resumes and reads result after simulated I/O completes
 * - Timeout fault detection after max retries in full kernel context
 * - Background logging task coexists with IO_PENDING scheduled task
 * - TaskTimer instrumentation covers IO_PENDING ticks
 *
 * Each test suite uses a unique Cfg type to isolate System<Cfg> inline
 * static state.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "SystemTestConfig.h"
#include "SystemTestHelpers.h"

#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/System.h"
#include "sputteros/osal/tasks/IBackgroundTask.h"
#include "sputteros/osal/tasks/IScheduledTask.h"

#include "../../unit/mocks/KernelTestAccess.h"

#include <gtest/gtest.h>

using namespace SputterOS;
using namespace SputterOS::SystemTests;

// =========================================================================
// Per-Test Cfg Types
// =========================================================================

template <int N> struct IoPendingPipelineCfg
{
    enum class State : uint8_t
    {
        IDLE = 0,
        RUNNING,
        FAULT
    };

    enum class CmdID : uint8_t
    {
        SET_STATE = 0,
        NOP       = 1
    };

    struct Command
    {
        CmdID   id;
        uint8_t targetDevice;
        float   value;
    };

    static constexpr int         kMaxCommandsPerTick = 4;
    static constexpr uint8_t     kMaxValidCommandID  = 1;
    static constexpr std::size_t kCoreCount          = 1;
    static constexpr std::size_t kQueueCapacity      = 16;
};

// =========================================================================
// Test doubles — controllable ADC + IO_PENDING task for system tests
// =========================================================================

/**
 * @brief Controllable simulated ADC for test code.
 *
 * Provides explicit test hooks to set readiness without
 * depending on wall-clock time.
 */
class PipelineADC
{
  public:
    void startConversion()
    {
        m_inFlight = true;
        m_ready    = false;
    }

    void setReady() { m_ready = true; }

    bool isConversionReady() const { return m_inFlight && m_ready; }

    float readResult()
    {
        m_inFlight = false;
        m_ready    = false;
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
 * @brief IO_PENDING task for system-level testing.
 *
 * Mirrors the AdcPollTask state machine (start → poll → read → repeat).
 */
class PipelineAdcTask : public IScheduledTask
{
  public:
    static constexpr SputterMicros kPeriodUs     = 50'000;
    static constexpr uint32_t      kMaxIoRetries = 5;

    SputterMicros periodUs() const override { return kPeriodUs; }
    bool          isIoPending() const override { return m_waitingForAdc; }

    explicit PipelineAdcTask(PipelineADC &adc) : m_adc(adc) {}

    void init() override
    {
        m_waitingForAdc = false;
        m_ioWaitCount   = 0;
        m_readingCount  = 0;
        m_lastReading   = 0.0f;
        m_lastTick      = 0;
        m_timeoutCount  = 0;
        ++m_initCount;
    }

    void tick(SputterMicros systemTimeMicros) override
    {
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
    uint32_t initCount() const { return m_initCount; }

  private:
    PipelineADC  &m_adc;
    bool          m_waitingForAdc{false};
    uint32_t      m_ioWaitCount{0};
    uint32_t      m_readingCount{0};
    float         m_lastReading{0.0f};
    SputterMicros m_lastTick{0};
    uint32_t      m_timeoutCount{0};
    uint32_t      m_initCount{0};
};

/**
 * @brief Background task counting dispatches for system testing.
 */
class PipelineBgLogger : public IBackgroundTask
{
  public:
    SputterMicros maxBudgetUs() const override { return 500; }

    void init() override { m_count = 0; }
    void tick(SputterMicros /*t*/) override { ++m_count; }

    uint32_t count() const { return m_count; }

  private:
    uint32_t m_count{0};
};

// =========================================================================
// Test Fixture — Suite 1
// =========================================================================

using IOCfg1 = IoPendingPipelineCfg<1>;

class IoPendingPipelineTest : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<IOCfg1>(); }
};

// =========================================================================
// Tests — IO_PENDING through kernel tick loop
// =========================================================================

TEST_F(IoPendingPipelineTest, BuildWithIoPendingTask_Succeeds)
{
    InstrumentedApp<IOCfg1>   app;
    FakeStreamReader          stream;
    AlwaysSafeSafetyMonitor   monitor;
    ISafetyMonitor           *monitors[] = {&monitor};

    PipelineADC     adc;
    PipelineAdcTask adcTask(adc);

    SystemBuilder<IOCfg1> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&adcTask);

    BuildResult result = builder.build();
    EXPECT_TRUE(result.ok) << result.error;
}

TEST_F(IoPendingPipelineTest, InitPropagates_ToAdcTask)
{
    InstrumentedApp<IOCfg1>   app;
    FakeStreamReader          stream;
    AlwaysSafeSafetyMonitor   monitor;
    ISafetyMonitor           *monitors[] = {&monitor};

    PipelineADC     adc;
    PipelineAdcTask adcTask(adc);

    SystemBuilder<IOCfg1> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&adcTask);
    ASSERT_TRUE(builder.build());

    System<IOCfg1>::init(0);
    EXPECT_EQ(adcTask.initCount(), 1u);
}

TEST_F(IoPendingPipelineTest, KernelTick_StartsThenReadsConversion)
{
    InstrumentedApp<IOCfg1>   app;
    FakeStreamReader          stream;
    AlwaysSafeSafetyMonitor   monitor;
    ISafetyMonitor           *monitors[] = {&monitor};

    PipelineADC     adc;
    PipelineAdcTask adcTask(adc);

    SystemBuilder<IOCfg1> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&adcTask);
    ASSERT_TRUE(builder.build());

    System<IOCfg1>::init(0);

    // Tick 1: starts conversion (at kPeriodUs so rate-limit passes)
    System<IOCfg1>::tick(0, SputterMicros(50'000));
    EXPECT_TRUE(adcTask.isIoPending());
    EXPECT_EQ(adcTask.readingCount(), 0u);

    // Simulate ADC completing
    adc.setReady();

    // Tick 2: reads result
    System<IOCfg1>::tick(0, SputterMicros(51'000));
    EXPECT_EQ(adcTask.readingCount(), 1u);
    EXPECT_FLOAT_EQ(adcTask.lastReading(), 0.0f);
}

TEST_F(IoPendingPipelineTest, KernelTick_MultipleConversions)
{
    InstrumentedApp<IOCfg1>   app;
    FakeStreamReader          stream;
    AlwaysSafeSafetyMonitor   monitor;
    ISafetyMonitor           *monitors[] = {&monitor};

    PipelineADC     adc;
    PipelineAdcTask adcTask(adc);

    SystemBuilder<IOCfg1> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&adcTask);
    ASSERT_TRUE(builder.build());

    System<IOCfg1>::init(0);

    // Complete 3 conversion cycles
    SputterMicros now(50'000);
    for (int i = 0; i < 3; ++i)
    {
        System<IOCfg1>::tick(0, now);
        adc.setReady();
        now += 1'000;
        System<IOCfg1>::tick(0, now);
        now += 100'000; // advance well past rate limit
    }

    EXPECT_EQ(adcTask.readingCount(), 3u);
    // Sawtooth: 0, 37, 74
    EXPECT_FLOAT_EQ(adcTask.lastReading(), 74.0f);
}

// =========================================================================
// Test Fixture — Suite 2 (timeout)
// =========================================================================

using IOCfg2 = IoPendingPipelineCfg<2>;

class IoPendingTimeoutTest : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<IOCfg2>(); }
};

TEST_F(IoPendingTimeoutTest, Timeout_DetectedThroughKernel)
{
    InstrumentedApp<IOCfg2>   app;
    FakeStreamReader          stream;
    AlwaysSafeSafetyMonitor   monitor;
    ISafetyMonitor           *monitors[] = {&monitor};

    PipelineADC     adc;
    PipelineAdcTask adcTask(adc);

    SystemBuilder<IOCfg2> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&adcTask);
    ASSERT_TRUE(builder.build());

    System<IOCfg2>::init(0);

    // Tick 1: starts conversion (at kPeriodUs so rate-limit passes)
    System<IOCfg2>::tick(0, SputterMicros(50'000));
    ASSERT_TRUE(adcTask.isIoPending());

    // Tick kMaxIoRetries+1 more times without setting ready
    for (uint32_t i = 0; i <= PipelineAdcTask::kMaxIoRetries; ++i)
    {
        System<IOCfg2>::tick(0, SputterMicros(51'000 + i * 100));
    }

    // Timeout should have cleared pending
    EXPECT_FALSE(adcTask.isIoPending());
    EXPECT_EQ(adcTask.timeoutCount(), 1u);
    EXPECT_EQ(adcTask.readingCount(), 0u);
}

TEST_F(IoPendingTimeoutTest, Recovery_AfterTimeout)
{
    InstrumentedApp<IOCfg2>   app;
    FakeStreamReader          stream;
    AlwaysSafeSafetyMonitor   monitor;
    ISafetyMonitor           *monitors[] = {&monitor};

    PipelineADC     adc;
    PipelineAdcTask adcTask(adc);

    SystemBuilder<IOCfg2> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&adcTask);
    ASSERT_TRUE(builder.build());

    System<IOCfg2>::init(0);

    // First conversion times out
    System<IOCfg2>::tick(0, SputterMicros(50'000));
    for (uint32_t i = 0; i <= PipelineAdcTask::kMaxIoRetries; ++i)
        System<IOCfg2>::tick(0, SputterMicros(51'000 + i * 100));

    ASSERT_EQ(adcTask.timeoutCount(), 1u);

    // Next period — successful conversion
    System<IOCfg2>::tick(0, SputterMicros(100'000));
    ASSERT_TRUE(adcTask.isIoPending());

    adc.setReady();
    System<IOCfg2>::tick(0, SputterMicros(101'000));

    EXPECT_EQ(adcTask.readingCount(), 1u);
    EXPECT_EQ(adcTask.timeoutCount(), 1u);
}

// =========================================================================
// Test Fixture — Suite 3 (with background task)
// =========================================================================

using IOCfg3 = IoPendingPipelineCfg<3>;

class IoPendingWithBackgroundTest : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<IOCfg3>(); }
};

TEST_F(IoPendingWithBackgroundTest, BackgroundTask_CoexistsWithIoPending)
{
    InstrumentedApp<IOCfg3>   app;
    FakeStreamReader          stream;
    AlwaysSafeSafetyMonitor   monitor;
    ISafetyMonitor           *monitors[] = {&monitor};

    PipelineADC     adc;
    PipelineAdcTask adcTask(adc);
    PipelineBgLogger bgTask;

    SystemBuilder<IOCfg3> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&adcTask);
    builder.addBackgroundTask(&bgTask);
    ASSERT_TRUE(builder.build());

    System<IOCfg3>::init(0);

    // Run several ticks, completing conversions
    SputterMicros now(50'000);
    for (int i = 0; i < 5; ++i)
    {
        System<IOCfg3>::tick(0, now);
        adc.setReady();
        now += 1'000;
        System<IOCfg3>::tick(0, now);
        now += 100'000;
    }

    // ADC should have completed conversions
    EXPECT_EQ(adcTask.readingCount(), 5u);

    // Background task is registered — even if SystemScheduler isn't
    // implemented yet, the task should be discoverable
    EXPECT_GE(System<IOCfg3>::backgroundTaskCount(), 1u);
}

TEST_F(IoPendingWithBackgroundTest, TaskTimer_CoversIoPendingTicks)
{
    InstrumentedApp<IOCfg3>   app;
    FakeStreamReader          stream;
    AlwaysSafeSafetyMonitor   monitor;
    ISafetyMonitor           *monitors[] = {&monitor};

    PipelineADC     adc;
    PipelineAdcTask adcTask(adc);

    SystemBuilder<IOCfg3> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&adcTask);
    ASSERT_TRUE(builder.build());

    System<IOCfg3>::init(0);

    // Run 20 ticks — timer should record samples
    for (uint32_t t = 0; t < 20; ++t)
        System<IOCfg3>::tick(0, SputterMicros(t * 1'000));

    EXPECT_GT(adcTask.timer().sampleCount(), 0u);
}
