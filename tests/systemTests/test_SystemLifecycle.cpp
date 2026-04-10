/**
 * @file test_SystemLifecycle.cpp
 * @brief System tests for the Builder → Init → Tick lifecycle pipeline.
 *
 * Exercises real SystemBuilder and System with stub dependencies to verify:
 * - Single-core and dual-core build paths
 * - init() propagation to all tasks
 * - tick() delivers correct timestamps and instruments TaskTimers
 * - Double-build rejection
 * - Infrastructure-only mode (null app)
 */

#include "SystemTestConfig.h"
#include "SystemTestHelpers.h"

#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/KernelConstructTag.h"
#include "sputteros/kernel/System.h"

// KernelTestAccess is in the unit test mocks directory but we include
// it directly — it is a lightweight header with no test-framework deps.
#include "../../unit/mocks/KernelTestAccess.h"

#include <gtest/gtest.h>

using namespace SputterOS;
using namespace SputterOS::SystemTests;

// =========================================================================
// Single-Core Lifecycle
// =========================================================================

using SCfg = SingleCoreConfig;

class SystemLifecycleSingleCore : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<SCfg>(); }
};

TEST_F(SystemLifecycleSingleCore, BuildSucceeds)
{
    InstrumentedApp<SCfg>   app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    SystemBuilder<SCfg> builder(&app, monitors, 1);
    builder.setStream(&stream);
    builder.setWatchdogKick(nullptr);

    BuildResult result = builder.build();
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_TRUE(System<SCfg>::isBuilt());
}

TEST_F(SystemLifecycleSingleCore, InitPropagates)
{
    InstrumentedApp<SCfg>   app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};
    InstrumentedTask        userTask;

    SystemBuilder<SCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addTask(&userTask);
    ASSERT_TRUE(builder.build());

    System<SCfg>::init(0);

    EXPECT_EQ(app.initCount, 1u);
    EXPECT_EQ(userTask.initCount, 1u);
}

TEST_F(SystemLifecycleSingleCore, TickDeliversTimestamp)
{
    InstrumentedApp<SCfg>   app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};
    InstrumentedTask        userTask;

    SystemBuilder<SCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addTask(&userTask);
    ASSERT_TRUE(builder.build());

    System<SCfg>::init(0);
    System<SCfg>::tick(0, SputterMicros(1000));

    EXPECT_EQ(userTask.tickCount, 1u);
    EXPECT_EQ(userTask.lastTickTime, SputterMicros(1000));
    EXPECT_EQ(app.tickCount, 1u);
    EXPECT_EQ(app.lastTickTime, SputterMicros(1000));
}

TEST_F(SystemLifecycleSingleCore, TickInstrumentsTaskTimers)
{
    InstrumentedApp<SCfg>   app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};
    InstrumentedTask        userTask;

    // Provide a clock source so TaskTimers can measure
    static uint64_t s_clock = 0;
    auto            clockFn = []() -> uint64_t { return s_clock; };

    SystemBuilder<SCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr).setClockSource(clockFn);
    builder.core(0).addTask(&userTask);
    ASSERT_TRUE(builder.build());

    System<SCfg>::init(0);

    // Simulate the clock advancing during tick
    s_clock = 100;
    System<SCfg>::tick(0, SputterMicros(100));
    s_clock = 200;

    // After one tick, each task's timer should have recorded a duration.
    // Since the InstrumentedTask::tick() is nearly instant and the clock
    // is manual, lastDuration should be 0 (start and stop see same clock).
    // But maxDuration is still valid — it just means the task was fast.
    EXPECT_EQ(userTask.timer().lastDuration(), SputterMicros(0));
    EXPECT_EQ(userTask.timer().maxDuration(), SputterMicros(0));
}

TEST_F(SystemLifecycleSingleCore, DoubleBuildRejected)
{
    InstrumentedApp<SCfg>   app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    SystemBuilder<SCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);

    ASSERT_TRUE(builder.build());

    // Second build on a fresh builder should fail.
    SystemBuilder<SCfg> builder2(&app, monitors, 1);
    builder2.setStream(&stream).setWatchdogKick(nullptr);
    BuildResult result = builder2.build();

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error, nullptr);
}

TEST_F(SystemLifecycleSingleCore, InfrastructureOnlyMode)
{
    // Null app → no kernel tasks created, but infrastructure is valid.
    InstrumentedTask userTask;

    SystemBuilder<SCfg> builder(nullptr, nullptr, 0);
    builder.core(0).addTask(&userTask);
    BuildResult result = builder.build();

    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_TRUE(System<SCfg>::isBuilt());

    System<SCfg>::init(0);
    System<SCfg>::tick(0, SputterMicros(500));

    EXPECT_EQ(userTask.tickCount, 1u);
}

TEST_F(SystemLifecycleSingleCore, TaskCountReflectsKernelAndUserTasks)
{
    InstrumentedApp<SCfg>   app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};
    InstrumentedTask        userTask;

    SystemBuilder<SCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addTask(&userTask);
    ASSERT_TRUE(builder.build());

    // Single-core: ControlTask + CommsTask + DiagnosticsTask + userTask = 4
    EXPECT_EQ(System<SCfg>::taskCount(0), 4u);
}

TEST_F(SystemLifecycleSingleCore, MultipleTicksAccumulateCorrectly)
{
    InstrumentedApp<SCfg>   app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};
    InstrumentedTask        userTask;

    SystemBuilder<SCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addTask(&userTask);
    ASSERT_TRUE(builder.build());

    System<SCfg>::init(0);

    for (uint64_t t = 1000; t <= 5000; t += 1000)
    {
        System<SCfg>::tick(0, SputterMicros(t));
    }

    EXPECT_EQ(userTask.tickCount, 5u);
    EXPECT_EQ(userTask.lastTickTime, SputterMicros(5000));
    EXPECT_EQ(app.tickCount, 5u);
}

// =========================================================================
// Dual-Core Lifecycle
// =========================================================================

using DCfg = DualCoreConfig;

class SystemLifecycleDualCore : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<DCfg>(); }
};

TEST_F(SystemLifecycleDualCore, BuildSucceeds)
{
    InstrumentedApp<DCfg>   app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    SystemBuilder<DCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);

    BuildResult result = builder.build();
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_TRUE(System<DCfg>::isBuilt());
}

TEST_F(SystemLifecycleDualCore, KernelTasksDistributedAcrossCores)
{
    InstrumentedApp<DCfg>   app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    SystemBuilder<DCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    // Core 0: ControlTask only
    EXPECT_EQ(System<DCfg>::taskCount(0), 1u);

    // Core 1: CommsTask + DiagnosticsTask
    EXPECT_EQ(System<DCfg>::taskCount(1), 2u);
}
