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
 * - Kernel-managed period dispatch (kernelManagedPeriod opt-in skip) [§2.2]
 * - Priority-ordered dispatch (effectivePriority sort, auto-RMS) [§2.3]
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
    InstrumentedApp<MRCfg1> app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

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
    InstrumentedApp<MRCfg1> app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

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
    InstrumentedApp<MRCfg1> app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    PipelineFastTask fast;
    PipelineSlowTask slow(fast);

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
    InstrumentedApp<MRCfg1> app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    PipelineFastTask fast;
    PipelineSlowTask slow(fast);

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
    InstrumentedApp<MRCfg2> app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    PipelineFastTask fast;
    PipelineSlowTask slow(fast);

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
    InstrumentedApp<MRCfg3> app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    PipelineFastTask fast;
    PipelineSlowTask slow(fast);

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

// =========================================================================
// Suite: Kernel-Managed Period Dispatch (§2.2)
// =========================================================================

/**
 * @brief Scheduled task that opts in to kernel-managed period dispatch.
 *
 * The kernel will skip this task when its declared period has not
 * elapsed, so the task itself does NO internal rate-limiting.
 */
class KernelManagedTask : public IScheduledTask
{
  public:
    explicit KernelManagedTask(SputterMicros period) : m_period(period) {}

    SputterMicros periodUs() const override { return m_period; }
    bool          kernelManagedPeriod() const override { return true; }

    void init() override { m_tickCount = 0; }
    void tick(SputterMicros /*t*/) override { ++m_tickCount; }

    uint32_t tickCount() const { return m_tickCount; }

  private:
    SputterMicros m_period;
    uint32_t      m_tickCount{0};
};

using KMCfg1 = MultiRatePipelineCfg<10>;

class KernelManagedPeriodTest : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<KMCfg1>(); }
};

TEST_F(KernelManagedPeriodTest, TaskSkippedBeforePeriodElapses)
{
    InstrumentedApp<KMCfg1> app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    KernelManagedTask task(10'000); // 10 ms period

    SystemBuilder<KMCfg1> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&task);
    ASSERT_TRUE(builder.build());
    System<KMCfg1>::init(0);

    // First tick always dispatches (lastDispatch == 0).
    System<KMCfg1>::tick(0, SputterMicros(1'000));
    EXPECT_EQ(task.tickCount(), 1u);

    // Tick before period elapses — task should be skipped.
    System<KMCfg1>::tick(0, SputterMicros(5'000));
    EXPECT_EQ(task.tickCount(), 1u);

    // Tick after period elapses — task should fire again.
    System<KMCfg1>::tick(0, SputterMicros(11'000));
    EXPECT_EQ(task.tickCount(), 2u);

    // Another tick within second period — still 2.
    System<KMCfg1>::tick(0, SputterMicros(15'000));
    EXPECT_EQ(task.tickCount(), 2u);

    // Third period boundary.
    System<KMCfg1>::tick(0, SputterMicros(21'000));
    EXPECT_EQ(task.tickCount(), 3u);
}

TEST_F(KernelManagedPeriodTest, MultipleTasksAtDifferentRates)
{
    InstrumentedApp<KMCfg1> app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    KernelManagedTask fast(10'000);   // 10 ms
    KernelManagedTask slow(100'000);  // 100 ms

    SystemBuilder<KMCfg1> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&fast);
    builder.core(0).addScheduledTask(&slow);
    ASSERT_TRUE(builder.build());
    System<KMCfg1>::init(0);

    // Run for 200 ms at 5 ms ticks
    for (uint32_t t = 0; t < 40; ++t)
    {
        System<KMCfg1>::tick(0, SputterMicros(t * 5'000));
    }

    // Fast (10ms period): ~20 dispatches in 200ms
    EXPECT_GE(fast.tickCount(), 18u);
    EXPECT_LE(fast.tickCount(), 22u);

    // Slow (100ms period): ~2 dispatches in 200ms
    EXPECT_GE(slow.tickCount(), 1u);
    EXPECT_LE(slow.tickCount(), 3u);
}

// =========================================================================
// Suite: Priority-Ordered Dispatch (§2.3)
// =========================================================================

/**
 * @brief Task that records its dispatch order in a shared vector.
 */
class OrderTrackingTask : public IScheduledTask
{
  public:
    OrderTrackingTask(SputterMicros period, uint8_t priority, uint32_t id,
                      uint32_t *orderLog, uint32_t &orderIdx)
        : m_period(period), m_priority(priority), m_id(id),
          m_orderLog(orderLog), m_orderIdx(orderIdx)
    {
    }

    SputterMicros periodUs() const override { return m_period; }
    uint8_t       schedulePriority() const override { return m_priority; }

    void init() override {}
    void tick(SputterMicros /*t*/) override
    {
        if (m_orderIdx < 64)
            m_orderLog[m_orderIdx++] = m_id;
    }

  private:
    SputterMicros m_period;
    uint8_t       m_priority;
    uint32_t      m_id;
    uint32_t     *m_orderLog;
    uint32_t     &m_orderIdx;
};

using PriCfg1 = MultiRatePipelineCfg<11>;

class PriorityOrderTest : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<PriCfg1>(); }
};

TEST_F(PriorityOrderTest, HigherPriorityTaskDispatchedFirst)
{
    InstrumentedApp<PriCfg1> app;
    FakeStreamReader         stream;
    AlwaysSafeSafetyMonitor  monitor;
    ISafetyMonitor          *monitors[] = {&monitor};

    uint32_t orderLog[64] = {};
    uint32_t orderIdx     = 0;

    // Register tasks in reverse priority order.
    // Priority 30 (low) added first, priority 5 (high) added last.
    OrderTrackingTask lowPri(10'000, 30, 3, orderLog, orderIdx);
    OrderTrackingTask medPri(10'000, 15, 2, orderLog, orderIdx);
    OrderTrackingTask hiPri(10'000, 5, 1, orderLog, orderIdx);

    SystemBuilder<PriCfg1> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    // Add in reverse priority order to verify sorting.
    builder.core(0).addScheduledTask(&lowPri);
    builder.core(0).addScheduledTask(&medPri);
    builder.core(0).addScheduledTask(&hiPri);
    ASSERT_TRUE(builder.build());
    System<PriCfg1>::init(0);

    System<PriCfg1>::tick(0, SputterMicros(1'000));

    // Kernel tasks (ControlTask=0, CommsTask=1) dispatch first,
    // then user tasks in priority order: hiPri(5), medPri(15), lowPri(30).
    ASSERT_GE(orderIdx, 3u);
    // User tasks are the last 3 entries in the dispatch log.
    EXPECT_EQ(orderLog[orderIdx - 3], 1u); // hiPri
    EXPECT_EQ(orderLog[orderIdx - 2], 2u); // medPri
    EXPECT_EQ(orderLog[orderIdx - 1], 3u); // lowPri
}

TEST_F(PriorityOrderTest, AutoRMSAssignsShortPeriodHigherPriority)
{
    InstrumentedApp<PriCfg1> app;
    FakeStreamReader         stream;
    AlwaysSafeSafetyMonitor  monitor;
    ISafetyMonitor          *monitors[] = {&monitor};

    uint32_t orderLog[64] = {};
    uint32_t orderIdx     = 0;

    // All tasks use default schedulePriority() == 0xFF → auto-RMS.
    // Shorter period should get higher effective priority.
    OrderTrackingTask longPeriod(100'000, 0xFF, 3, orderLog, orderIdx);
    OrderTrackingTask shortPeriod(1'000, 0xFF, 1, orderLog, orderIdx);
    OrderTrackingTask midPeriod(10'000, 0xFF, 2, orderLog, orderIdx);

    SystemBuilder<PriCfg1> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    // Add in arbitrary order.
    builder.core(0).addScheduledTask(&longPeriod);
    builder.core(0).addScheduledTask(&shortPeriod);
    builder.core(0).addScheduledTask(&midPeriod);
    ASSERT_TRUE(builder.build());
    System<PriCfg1>::init(0);

    System<PriCfg1>::tick(0, SputterMicros(1'000));

    // After kernel tasks, user tasks should be in RMS order:
    // shortPeriod(1ms) → midPeriod(10ms) → longPeriod(100ms)
    ASSERT_GE(orderIdx, 3u);
    EXPECT_EQ(orderLog[orderIdx - 3], 1u); // shortPeriod
    EXPECT_EQ(orderLog[orderIdx - 2], 2u); // midPeriod
    EXPECT_EQ(orderLog[orderIdx - 1], 3u); // longPeriod
}

// =========================================================================
// Suite: KernelManagedPeriod Edge Cases
// =========================================================================

using KMEdgeCfg = MultiRatePipelineCfg<20>;

class KernelManagedPeriodEdgeTest : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<KMEdgeCfg>(); }
};

TEST_F(KernelManagedPeriodEdgeTest, PeriodZero_NeverSkips)
{
    InstrumentedApp<KMEdgeCfg> app;
    FakeStreamReader           stream;
    AlwaysSafeSafetyMonitor    monitor;
    ISafetyMonitor            *monitors[] = {&monitor};

    KernelManagedTask task(0); // period = 0

    SystemBuilder<KMEdgeCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&task);
    ASSERT_TRUE(builder.build());
    System<KMEdgeCfg>::init(0);

    for (uint32_t t = 1; t <= 10; ++t)
        System<KMEdgeCfg>::tick(0, SputterMicros(t * 100));

    // period=0 should never skip — task runs every tick
    EXPECT_EQ(task.tickCount(), 10u);
}

TEST_F(KernelManagedPeriodEdgeTest, IdenticalTimestamps_SkipsAfterFirst)
{
    InstrumentedApp<KMEdgeCfg> app;
    FakeStreamReader           stream;
    AlwaysSafeSafetyMonitor    monitor;
    ISafetyMonitor            *monitors[] = {&monitor};

    KernelManagedTask task(10'000);

    SystemBuilder<KMEdgeCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&task);
    ASSERT_TRUE(builder.build());
    System<KMEdgeCfg>::init(0);

    // First tick dispatches
    System<KMEdgeCfg>::tick(0, SputterMicros(1'000));
    EXPECT_EQ(task.tickCount(), 1u);

    // Same timestamp — period not elapsed, should skip
    System<KMEdgeCfg>::tick(0, SputterMicros(1'000));
    EXPECT_EQ(task.tickCount(), 1u);
}

TEST_F(KernelManagedPeriodEdgeTest, MixedManagedAndUnmanaged)
{
    InstrumentedApp<KMEdgeCfg> app;
    FakeStreamReader           stream;
    AlwaysSafeSafetyMonitor    monitor;
    ISafetyMonitor            *monitors[] = {&monitor};

    // Managed: skips when period not elapsed
    KernelManagedTask managed(50'000);

    // Unmanaged: ticks every call (self-rate-limits internally)
    // Use PipelineFastTask which has kernelManagedPeriod()=false
    PipelineFastTask unmanaged;

    SystemBuilder<KMEdgeCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&managed);
    builder.core(0).addScheduledTask(&unmanaged);
    ASSERT_TRUE(builder.build());
    System<KMEdgeCfg>::init(0);

    for (uint32_t t = 1; t <= 20; ++t)
        System<KMEdgeCfg>::tick(0, SputterMicros(t * 5'000));

    // Unmanaged should be called every tick (timer dispatches always)
    // The PipelineFastTask self-rate-limits at 10ms, so it gets ~10 samples
    EXPECT_GE(unmanaged.sampleCount(), 8u);
    EXPECT_LE(unmanaged.sampleCount(), 12u);

    // Managed at 50ms period, 20 ticks of 5ms = 100ms → ~2 dispatches
    EXPECT_GE(managed.tickCount(), 1u);
    EXPECT_LE(managed.tickCount(), 3u);
}

// =========================================================================
// Suite: Priority Sort Edge Cases
// =========================================================================

using PriEdgeCfg = MultiRatePipelineCfg<21>;

class PriorityEdgeTest : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<PriEdgeCfg>(); }
};

TEST_F(PriorityEdgeTest, AllSamePriority_StableRegistrationOrder)
{
    InstrumentedApp<PriEdgeCfg> app;
    FakeStreamReader            stream;
    AlwaysSafeSafetyMonitor     monitor;
    ISafetyMonitor             *monitors[] = {&monitor};

    uint32_t orderLog[64] = {};
    uint32_t orderIdx     = 0;

    // All tasks same explicit priority — should maintain registration order
    OrderTrackingTask a(10'000, 50, 1, orderLog, orderIdx);
    OrderTrackingTask b(10'000, 50, 2, orderLog, orderIdx);
    OrderTrackingTask c(10'000, 50, 3, orderLog, orderIdx);

    SystemBuilder<PriEdgeCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&a);
    builder.core(0).addScheduledTask(&b);
    builder.core(0).addScheduledTask(&c);
    ASSERT_TRUE(builder.build());
    System<PriEdgeCfg>::init(0);

    System<PriEdgeCfg>::tick(0, SputterMicros(1'000));

    // Stable sort: registration order preserved for equal priorities
    // Kernel tasks (controlTask=0, commsTask=1) come first, then user tasks
    ASSERT_GE(orderIdx, 3u);
    EXPECT_EQ(orderLog[orderIdx - 3], 1u);
    EXPECT_EQ(orderLog[orderIdx - 2], 2u);
    EXPECT_EQ(orderLog[orderIdx - 1], 3u);
}

TEST_F(PriorityEdgeTest, SingleUserTask_NoSortIssue)
{
    InstrumentedApp<PriEdgeCfg> app;
    FakeStreamReader            stream;
    AlwaysSafeSafetyMonitor     monitor;
    ISafetyMonitor             *monitors[] = {&monitor};

    uint32_t orderLog[64] = {};
    uint32_t orderIdx     = 0;

    OrderTrackingTask only(10'000, 0xFF, 42, orderLog, orderIdx);

    SystemBuilder<PriEdgeCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&only);
    ASSERT_TRUE(builder.build());
    System<PriEdgeCfg>::init(0);

    System<PriEdgeCfg>::tick(0, SputterMicros(1'000));

    // Find the user task dispatch (kernel tasks dispatch first)
    bool found = false;
    for (uint32_t i = 0; i < orderIdx; ++i)
    {
        if (orderLog[i] == 42) { found = true; break; }
    }
    EXPECT_TRUE(found);
}

// =========================================================================
// Suite: Gap Budget Edge Cases
// =========================================================================

// Config with a very small control budget to test gap budget limits
template <int N> struct TightBudgetCfg
{
    enum class State : uint8_t { IDLE = 0 };
    enum class CmdID : uint8_t { NOP = 0 };
    struct Command
    {
        CmdID   id;
        uint8_t targetDevice;
        float   value;
    };

    static constexpr int         kMaxCommandsPerTick = 4;
    static constexpr uint8_t     kMaxValidCommandID  = 0;
    static constexpr std::size_t kCoreCount          = 1;
    static constexpr std::size_t kQueueCapacity      = 8;
    // Tiny budget to stress gap budget logic
    static constexpr SputterMicros kControlBudgetUs  = 100;
};

class SmallBudgetBackground : public IBackgroundTask
{
  public:
    SputterMicros maxBudgetUs() const override { return 200; }
    void          init() override { m_count = 0; }
    void          tick(SputterMicros) override { ++m_count; }
    uint32_t      count() const { return m_count; }

  private:
    uint32_t m_count{0};
};

using GapCfg1 = TightBudgetCfg<1>;

class GapBudgetEdgeTest : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<GapCfg1>(); }
};

TEST_F(GapBudgetEdgeTest, Phase1ExceedsBudget_FirstBgStillDispatches)
{
    InstrumentedApp<GapCfg1> app;
    FakeStreamReader         stream;
    AlwaysSafeSafetyMonitor  monitor;
    ISafetyMonitor          *monitors[] = {&monitor};

    SmallBudgetBackground bg1;
    SmallBudgetBackground bg2;

    SystemBuilder<GapCfg1> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.addBackgroundTask(&bg1);
    builder.addBackgroundTask(&bg2);
    ASSERT_TRUE(builder.build());
    System<GapCfg1>::init(0);

    // Tick — even with tiny budget, first bg task always dispatches
    System<GapCfg1>::tick(0, SputterMicros(1'000));

    // First bg task in round-robin should have dispatched
    uint32_t total = bg1.count() + bg2.count();
    EXPECT_GE(total, 1u);
}

// =========================================================================
// Suite: Deadline Miss Detection (§2.4)
// =========================================================================

class OverrunTrackingTask : public IScheduledTask
{
  public:
    SputterMicros periodUs() const override { return 10'000; }
    SputterMicros declaredWcetUs() const override { return m_wcet; }

    void init() override
    {
        m_tickCount     = 0;
        m_overrunCount  = 0;
        m_lastActualUs  = 0;
        m_lastBudgetUs  = 0;
    }

    void tick(SputterMicros) override { ++m_tickCount; }

    void onOverrun(SputterMicros actualUs, SputterMicros budgetUs) override
    {
        ++m_overrunCount;
        m_lastActualUs = actualUs;
        m_lastBudgetUs = budgetUs;
    }

    void setWcet(SputterMicros w) { m_wcet = w; }

    uint32_t      tickCount() const { return m_tickCount; }
    uint32_t      overrunCount() const { return m_overrunCount; }
    SputterMicros lastActualUs() const { return m_lastActualUs; }
    SputterMicros lastBudgetUs() const { return m_lastBudgetUs; }

  private:
    SputterMicros m_wcet{0};
    uint32_t      m_tickCount{0};
    uint32_t      m_overrunCount{0};
    SputterMicros m_lastActualUs{0};
    SputterMicros m_lastBudgetUs{0};
};

using DLCfg1 = MultiRatePipelineCfg<30>;

class DeadlineMissTest : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<DLCfg1>(); }
};

TEST_F(DeadlineMissTest, NoWcetDeclared_NoOverrunCallback)
{
    InstrumentedApp<DLCfg1> app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    OverrunTrackingTask task;
    task.setWcet(0); // no declared WCET

    SystemBuilder<DLCfg1> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&task);
    ASSERT_TRUE(builder.build());
    System<DLCfg1>::init(0);

    for (uint32_t t = 1; t <= 10; ++t)
        System<DLCfg1>::tick(0, SputterMicros(t * 1'000));

    EXPECT_EQ(task.overrunCount(), 0u);
}

TEST_F(DeadlineMissTest, WcetDeclared_NoOverrunWhenUnderBudget)
{
    InstrumentedApp<DLCfg1> app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    OverrunTrackingTask task;
    task.setWcet(1'000'000); // very generous WCET — should never trip

    SystemBuilder<DLCfg1> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&task);
    ASSERT_TRUE(builder.build());
    System<DLCfg1>::init(0);

    for (uint32_t t = 1; t <= 10; ++t)
        System<DLCfg1>::tick(0, SputterMicros(t * 1'000));

    EXPECT_EQ(task.overrunCount(), 0u);
}

// =========================================================================
// Suite: Public Reset / Soft-Restart (§3.4)
// =========================================================================

using ResetCfg = MultiRatePipelineCfg<40>;

class PublicResetTest : public ::testing::Test
{
  protected:
    void TearDown() override
    {
        // reset() already public — use directly
        if (System<ResetCfg>::isBuilt())
            System<ResetCfg>::reset();
    }
};

TEST_F(PublicResetTest, ResetClearsBuiltFlag)
{
    InstrumentedApp<ResetCfg> app;
    FakeStreamReader          stream;
    AlwaysSafeSafetyMonitor   monitor;
    ISafetyMonitor           *monitors[] = {&monitor};

    SystemBuilder<ResetCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    EXPECT_TRUE(System<ResetCfg>::isBuilt());
    System<ResetCfg>::reset();
    EXPECT_FALSE(System<ResetCfg>::isBuilt());
}

TEST_F(PublicResetTest, ResetAllowsRebuild)
{
    InstrumentedApp<ResetCfg> app;
    FakeStreamReader          stream;
    AlwaysSafeSafetyMonitor   monitor;
    ISafetyMonitor           *monitors[] = {&monitor};

    // Build once
    {
        SystemBuilder<ResetCfg> builder(&app, monitors, 1);
        builder.setStream(&stream).setWatchdogKick(nullptr);
        ASSERT_TRUE(builder.build());
    }

    System<ResetCfg>::reset();

    // Build again — should succeed
    {
        SystemBuilder<ResetCfg> builder(&app, monitors, 1);
        builder.setStream(&stream).setWatchdogKick(nullptr);
        EXPECT_TRUE(builder.build());
    }
}

TEST_F(PublicResetTest, ResetReturnsToUnconfigured)
{
    InstrumentedApp<ResetCfg> app;
    FakeStreamReader          stream;
    AlwaysSafeSafetyMonitor   monitor;
    ISafetyMonitor           *monitors[] = {&monitor};

    SystemBuilder<ResetCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<ResetCfg>::init(0);
    EXPECT_EQ(System<ResetCfg>::kernelState(), Kernel::KernelState::RUNNING);

    System<ResetCfg>::reset();
    EXPECT_EQ(System<ResetCfg>::kernelState(), Kernel::KernelState::UNCONFIGURED);
}

// =========================================================================
// Suite: Version Macro
// =========================================================================

#include "sputteros/Version.h"

TEST(VersionTest, MacrosAreDefined)
{
    EXPECT_EQ(SPUTTEROS_VERSION_MAJOR, 1);
    EXPECT_EQ(SPUTTEROS_VERSION_MINOR, 0);
    EXPECT_EQ(SPUTTEROS_VERSION_PATCH, 0);
    EXPECT_EQ(SPUTTEROS_VERSION_INT, 10000);
}

TEST(VersionTest, StructMatchesMacros)
{
    EXPECT_EQ(SputterOS::Version::major, SPUTTEROS_VERSION_MAJOR);
    EXPECT_EQ(SputterOS::Version::minor, SPUTTEROS_VERSION_MINOR);
    EXPECT_EQ(SputterOS::Version::patch, SPUTTEROS_VERSION_PATCH);
    EXPECT_EQ(SputterOS::Version::asInt, SPUTTEROS_VERSION_INT);
    EXPECT_STREQ(SputterOS::Version::string, SPUTTEROS_VERSION_STRING);
}
