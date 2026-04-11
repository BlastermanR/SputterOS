/**
 * @file test_SafetyAbortPipeline.cpp
 * @brief System tests for the safety monitor → forceSafeAbort() pipeline.
 *
 * Exercises the real ControlTask safety evaluation loop through System<Cfg>
 * to verify:
 * - An unsafe monitor causes forceSafeAbort() on the user application.
 * - Mixed safe + unsafe monitors still triggers an abort.
 * - All safe monitors produce no abort.
 * - Zero monitors produces no abort.
 * - The abort is delivered every tick while the monitor remains unsafe.
 * - Clearing the monitor flag stops further aborts.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "SystemTestConfig.h"
#include "SystemTestHelpers.h"

#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/System.h"

#include "../../unit/mocks/KernelTestAccess.h"

#include <gtest/gtest.h>

using namespace SputterOS;
using namespace SputterOS::SystemTests;

using Cfg = SingleCoreConfig;

// =========================================================================
// Fixture
// =========================================================================

class SafetyAbortPipeline : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<Cfg>(); }
};

// =========================================================================
// Tests
// =========================================================================

TEST_F(SafetyAbortPipeline, UnsafeMonitorTriggersAbort)
{
    InstrumentedApp<Cfg> app;
    FakeStreamReader     stream;
    TrippableMonitor     monitor;
    monitor.safeFlag           = false;
    ISafetyMonitor *monitors[] = {&monitor};

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<Cfg>::init(0);
    System<Cfg>::tick(0, SputterMicros(1000));

    EXPECT_GE(app.abortCount, 1u);
}

TEST_F(SafetyAbortPipeline, SafeMonitorNoAbort)
{
    InstrumentedApp<Cfg>    app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<Cfg>::init(0);
    System<Cfg>::tick(0, SputterMicros(1000));
    System<Cfg>::tick(0, SputterMicros(2000));
    System<Cfg>::tick(0, SputterMicros(3000));

    EXPECT_EQ(app.abortCount, 0u);
}

TEST_F(SafetyAbortPipeline, MixedMonitorsAbortWhenAnyUnsafe)
{
    InstrumentedApp<Cfg>    app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor safeMonitor;
    TrippableMonitor        unsafeMonitor;
    unsafeMonitor.safeFlag     = false;
    ISafetyMonitor *monitors[] = {&safeMonitor, &unsafeMonitor};

    SystemBuilder<Cfg> builder(&app, monitors, 2);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<Cfg>::init(0);
    System<Cfg>::tick(0, SputterMicros(1000));

    EXPECT_GE(app.abortCount, 1u);
}

TEST_F(SafetyAbortPipeline, ZeroMonitorsNoAbort)
{
    InstrumentedApp<Cfg> app;
    FakeStreamReader     stream;

    SystemBuilder<Cfg> builder(&app, nullptr, 0);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<Cfg>::init(0);
    System<Cfg>::tick(0, SputterMicros(1000));

    EXPECT_EQ(app.abortCount, 0u);
}

TEST_F(SafetyAbortPipeline, AbortCalledEveryTickWhileUnsafe)
{
    InstrumentedApp<Cfg> app;
    FakeStreamReader     stream;
    TrippableMonitor     monitor;
    monitor.safeFlag           = false;
    ISafetyMonitor *monitors[] = {&monitor};

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<Cfg>::init(0);
    for (uint64_t t = 1000; t <= 5000; t += 1000)
    {
        System<Cfg>::tick(0, SputterMicros(t));
    }

    // forceSafeAbort() must be called on every tick when the monitor stays unsafe.
    EXPECT_EQ(app.abortCount, 5u);
}

TEST_F(SafetyAbortPipeline, ClearingMonitorStopsAbort)
{
    InstrumentedApp<Cfg> app;
    FakeStreamReader     stream;
    TrippableMonitor     monitor;
    monitor.safeFlag           = false;
    ISafetyMonitor *monitors[] = {&monitor};

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<Cfg>::init(0);
    System<Cfg>::tick(0, SputterMicros(1000));
    ASSERT_GE(app.abortCount, 1u);

    monitor.safeFlag = true; // Restore safe state
    System<Cfg>::tick(0, SputterMicros(2000));

    // Abort count must not increase after the monitor clears.
    EXPECT_EQ(app.abortCount, 1u);
}
