/**
 * @file test_MultiRatePipeline.cpp
 * @brief System tests for multi-rate scheduled task dispatch through the kernel.
 *
 * Exercises real SystemBuilder → System → tick pipelines with multi-rate
 * scheduled tasks and background tasks to validate:
 * - Multiple scheduled tasks at different frequencies on a single core
 * - Data flow from fast sampler to slow reporter across tick iterations
 * - Background task registration and per-core task slot assignment
 * - Init propagation to all user tasks
 * - TaskTimer instrumentation for each user task
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

template <int N> struct MultiRatePipelineCfg
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
// Test doubles — multi-rate tasks for system integration
// =========================================================================

/**
 * @brief Fast-rate scheduled task (10 ms period) accumulating samples.
 */
class PipelineFastTask : public IScheduledTask
{
  public:
    static constexpr SputterMicros kPeriodUs = 10'000;

    SputterMicros periodUs() const override { return kPeriodUs; }
    SputterMicros declaredWcetUs() const override { return 50; }

    void init() override
    {
        m_sampleCount = 0;
        m_accumulator = 0;
        m_lastTick    = 0;
        ++m_initCount;
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

    uint32_t initCount() const { return m_initCount; }

  private:
    uint32_t      m_sampleCount{0};
    uint64_t      m_accumulator{0};
    SputterMicros m_lastTick{0};
    uint32_t      m_initCount{0};
};

/**
 * @brief Slow-rate scheduled task (500 ms period) draining fast task data.
 */
class PipelineSlowTask : public IScheduledTask
{
  public:
    static constexpr SputterMicros kPeriodUs = 500'000;

    SputterMicros periodUs() const override { return kPeriodUs; }

    explicit PipelineSlowTask(PipelineFastTask &sampler) : m_sampler(sampler) {}

    void init() override
    {
        m_reportCount      = 0;
        m_lastDrainedAccum = 0;
        m_lastTick         = 0;
        ++m_initCount;
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
    uint32_t initCount() const { return m_initCount; }

  private:
    PipelineFastTask &m_sampler;
    uint32_t          m_reportCount{0};
    uint64_t          m_lastDrainedAccum{0};
    SputterMicros     m_lastTick{0};
    uint32_t          m_initCount{0};
};

/**
 * @brief Background task counting dispatches.
 */
class PipelineBackgroundTask : public IBackgroundTask
{
  public:
    SputterMicros maxBudgetUs() const override { return 1000; }

    void init() override
    {
        m_dispatchCount = 0;
        ++m_initCount;
    }

    void tick(SputterMicros /*systemTimeMicros*/) override { ++m_dispatchCount; }

    uint32_t dispatchCount() const { return m_dispatchCount; }
    uint32_t initCount() const { return m_initCount; }

  private:
    uint32_t m_dispatchCount{0};
    uint32_t m_initCount{0};
};

// =========================================================================
// Test Fixture
// =========================================================================

using MRCfg1 = MultiRatePipelineCfg<1>;

class MultiRatePipelineTest : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<MRCfg1>(); }
};

// =========================================================================
// Tests
// =========================================================================

TEST_F(MultiRatePipelineTest, BuildWithMultiRateTasks_Succeeds)
{
    InstrumentedApp<MRCfg1>   app;
    FakeStreamReader          stream;
    AlwaysSafeSafetyMonitor   monitor;
    ISafetyMonitor           *monitors[] = {&monitor};

    PipelineFastTask       fast;
    PipelineSlowTask       slow(fast);
    PipelineBackgroundTask bg;

    SystemBuilder<MRCfg1> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&fast);
    builder.core(0).addScheduledTask(&slow);
    builder.addBackgroundTask(&bg);

    BuildResult result = builder.build();
    EXPECT_TRUE(result.ok) << result.error;
}

TEST_F(MultiRatePipelineTest, InitPropagates_ToAllTasks)
{
    InstrumentedApp<MRCfg1>   app;
    FakeStreamReader          stream;
    AlwaysSafeSafetyMonitor   monitor;
    ISafetyMonitor           *monitors[] = {&monitor};

    PipelineFastTask       fast;
    PipelineSlowTask       slow(fast);
    PipelineBackgroundTask bg;

    SystemBuilder<MRCfg1> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&fast);
    builder.core(0).addScheduledTask(&slow);
    builder.addBackgroundTask(&bg);
    ASSERT_TRUE(builder.build());

    System<MRCfg1>::init(0);

    EXPECT_EQ(fast.initCount(), 1u);
    EXPECT_EQ(slow.initCount(), 1u);
    // Background tasks are registered but init is called via System::init
    // which iterates all core tasks (background tasks are in the core array)
}

TEST_F(MultiRatePipelineTest, TickLoop_FastTaskSamples)
{
    InstrumentedApp<MRCfg1>   app;
    FakeStreamReader          stream;
    AlwaysSafeSafetyMonitor   monitor;
    ISafetyMonitor           *monitors[] = {&monitor};

    PipelineFastTask       fast;
    PipelineSlowTask       slow(fast);

    SystemBuilder<MRCfg1> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&fast);
    builder.core(0).addScheduledTask(&slow);
    ASSERT_TRUE(builder.build());

    System<MRCfg1>::init(0);

    // Run for 100 ms at 1 ms ticks (100 ticks)
    for (uint32_t t = 0; t < 100; ++t)
    {
        System<MRCfg1>::tick(0, SputterMicros(t * 1'000));
    }

    // Fast task at 10 ms should have fired ~10 times in 100 ms
    EXPECT_GE(fast.sampleCount(), 9u);
    EXPECT_LE(fast.sampleCount(), 11u);
}

TEST_F(MultiRatePipelineTest, TickLoop_SlowDrainsFast)
{
    InstrumentedApp<MRCfg1>   app;
    FakeStreamReader          stream;
    AlwaysSafeSafetyMonitor   monitor;
    ISafetyMonitor           *monitors[] = {&monitor};

    PipelineFastTask       fast;
    PipelineSlowTask       slow(fast);

    SystemBuilder<MRCfg1> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&fast);
    builder.core(0).addScheduledTask(&slow);
    ASSERT_TRUE(builder.build());

    System<MRCfg1>::init(0);

    // Run for 600 ms at 1 ms ticks — should get 1 slow report
    for (uint32_t t = 0; t < 600; ++t)
    {
        System<MRCfg1>::tick(0, SputterMicros(t * 1'000));
    }

    EXPECT_GE(slow.reportCount(), 1u);
    // Fast should have sampled ~60 times; accumulator was drained
    EXPECT_GE(fast.sampleCount(), 50u);
}

using MRCfg2 = MultiRatePipelineCfg<2>;

class MultiRateExtendedTest : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<MRCfg2>(); }
};

TEST_F(MultiRateExtendedTest, MultipleDrains_AccumulatorReset)
{
    InstrumentedApp<MRCfg2>   app;
    FakeStreamReader          stream;
    AlwaysSafeSafetyMonitor   monitor;
    ISafetyMonitor           *monitors[] = {&monitor};

    PipelineFastTask       fast;
    PipelineSlowTask       slow(fast);

    SystemBuilder<MRCfg2> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&fast);
    builder.core(0).addScheduledTask(&slow);
    ASSERT_TRUE(builder.build());

    System<MRCfg2>::init(0);

    // Run for 1200 ms — expect 2 slow reports
    for (uint32_t t = 0; t < 1200; ++t)
    {
        System<MRCfg2>::tick(0, SputterMicros(t * 1'000));
    }

    EXPECT_GE(slow.reportCount(), 2u);
    // After 2 drains, the current accumulator should only contain
    // samples since the last drain
    EXPECT_LT(fast.accumulator(), 5000u);
}

using MRCfg3 = MultiRatePipelineCfg<3>;

class MultiRateTimerTest : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<MRCfg3>(); }
};

TEST_F(MultiRateTimerTest, TaskTimers_InstrumentAllTasks)
{
    InstrumentedApp<MRCfg3>   app;
    FakeStreamReader          stream;
    AlwaysSafeSafetyMonitor   monitor;
    ISafetyMonitor           *monitors[] = {&monitor};

    PipelineFastTask       fast;
    PipelineSlowTask       slow(fast);

    SystemBuilder<MRCfg3> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&fast);
    builder.core(0).addScheduledTask(&slow);
    ASSERT_TRUE(builder.build());

    System<MRCfg3>::init(0);

    // Run a few ticks — TaskTimer should record non-zero sample count
    for (uint32_t t = 0; t < 20; ++t)
    {
        System<MRCfg3>::tick(0, SputterMicros(t * 1'000));
    }

    EXPECT_GT(fast.timer().sampleCount(), 0u);
    EXPECT_GT(slow.timer().sampleCount(), 0u);
}
