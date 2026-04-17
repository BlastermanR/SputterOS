/**
 * @file test_DiagnosticsMonitoring.cpp
 * @brief System tests for DiagnosticsTask health monitoring.
 *
 * Exercises the full DiagnosticsTask within a real System<Cfg> to verify:
 * - Budget violation detection logs to ErrorLogger
 * - Watchdog kick function is called
 * - ErrorLogger receives entries from kernel subsystems
 */

#include "SystemTestConfig.h"
#include "SystemTestHelpers.h"

#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/System.h"
#include "sputteros/osal/tasks/IScheduledTask.h"
#include "sputteros/utils/logging/ErrorLogger.h"

#include "../../unit/mocks/KernelTestAccess.h"

#include <gtest/gtest.h>

using namespace SputterOS;
using namespace SputterOS::SystemTests;

using Cfg = SingleCoreConfig;

// =========================================================================
// SlowTask — simulates a budget violation by advancing the clock
// =========================================================================

static uint64_t g_diagClock = 0;

class SlowTask : public IScheduledTask
{
  public:
    void init() override {}

    void tick(SputterMicros) override
    {
        // Simulate a task that takes desiredDurationUs by advancing clock
        g_diagClock += desiredDurationUs;
    }

    SputterMicros periodUs() const override { return 10000; }

    uint64_t desiredDurationUs = 0;
};

class DiagnosticsMonitoring : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        g_diagClock = 0;
        kickCount   = 0;
    }

    void TearDown() override { Kernel::KernelTestAccess::resetSystem<Cfg>(); }

    static uint32_t kickCount;

    static void watchdogKickFn() { ++kickCount; }
};

uint32_t DiagnosticsMonitoring::kickCount = 0;

TEST_F(DiagnosticsMonitoring, WatchdogKickCalledDuringTick)
{
    InstrumentedApp<Cfg>    app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    auto clockFn = []() -> uint64_t { return g_diagClock; };

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(&watchdogKickFn).setClockSource(clockFn);
    ASSERT_TRUE(builder.build());

    System<Cfg>::init(0);

    g_diagClock = 1000;
    System<Cfg>::tick(0, SputterMicros(1000));

    // DiagnosticsTask should have called the watchdog kick once per tick.
    EXPECT_GE(kickCount, 1u);
}

TEST_F(DiagnosticsMonitoring, BudgetViolationLogsError)
{
    InstrumentedApp<Cfg>    app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    SlowTask slowTask;
    // Set the desired duration to exceed the default 10ms (10000µs) budget
    slowTask.desiredDurationUs = 20000;

    auto clockFn = []() -> uint64_t { return g_diagClock; };

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr).setClockSource(clockFn);
    builder.core(0).addScheduledTask(&slowTask);
    ASSERT_TRUE(builder.build());

    System<Cfg>::init(0);

    // First tick: SlowTask advances clock by 20000µs during its tick.
    // System::tick instruments start/stop around each task, so the timer
    // will record lastDuration = 20000µs (over 10000µs budget).
    g_diagClock = 1000;
    System<Cfg>::tick(0, SputterMicros(1000));

    // Second tick: DiagnosticsTask checks timers and should log the violation.
    g_diagClock += 1000;
    System<Cfg>::tick(0, SputterMicros(g_diagClock));

    // Check that a SENSOR_ERROR was logged (DiagnosticsTask uses this code
    // for budget violations — see DiagnosticsTask.cpp).
    bool               found = false;
    ErrorLogger::Entry entry{};
    while (System<Cfg>::errorLogger().read(entry))
    {
        if (entry.code == ErrorLogger::ErrorCode::SENSOR_ERROR)
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected budget violation entry in ErrorLogger";
}

TEST_F(DiagnosticsMonitoring, NoViolationWhenUnderBudget)
{
    InstrumentedApp<Cfg>    app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    auto clockFn = []() -> uint64_t { return g_diagClock; };

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr).setClockSource(clockFn);
    ASSERT_TRUE(builder.build());

    System<Cfg>::init(0);

    // Tick several times with fast clock progression (well under budget)
    for (uint64_t t = 1000; t <= 5000; t += 1000)
    {
        g_diagClock = t;
        System<Cfg>::tick(0, SputterMicros(t));
    }

    // No SENSOR_ERROR should have been logged
    ErrorLogger::Entry entry{};
    while (System<Cfg>::errorLogger().read(entry))
    {
        EXPECT_NE(entry.code, ErrorLogger::ErrorCode::SENSOR_ERROR) << "Unexpected budget violation logged";
    }
}

TEST_F(DiagnosticsMonitoring, ErrorLoggerAccessibleThroughSystem)
{
    InstrumentedApp<Cfg>    app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    // Manually log an entry and verify retrieval
    System<Cfg>::errorLogger().log(ErrorLogger::ErrorCode::INTERLOCK_TRIP, SputterMicros(42), 99.9f);

    EXPECT_EQ(System<Cfg>::errorLogger().count(), 1u);

    ErrorLogger::Entry entry{};
    ASSERT_TRUE(System<Cfg>::errorLogger().read(entry));
    EXPECT_EQ(entry.code, ErrorLogger::ErrorCode::INTERLOCK_TRIP);
    EXPECT_EQ(entry.timestamp, SputterMicros(42));
    EXPECT_FLOAT_EQ(entry.value, 99.9f);
}
