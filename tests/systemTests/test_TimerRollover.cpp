/**
 * @file test_TimerRollover.cpp
 * @brief System tests for timer rollover fault detection.
 *
 * System::tick() guards against buggy clock sources that might wrap.
 * These tests verify:
 * - Forward progression: no fault logged
 * - Rollover (t2 < t1): TIMER_ROLLOVER logged to ErrorLogger
 * - Stalled clock (t2 == t1): no spurious fault
 * - ErrorLogger entry contains correct code and timestamp
 */

#include "SystemTestConfig.h"
#include "SystemTestHelpers.h"

#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/System.h"
#include "sputteros/utils/logging/ErrorLogger.h"

#include "../../unit/mocks/KernelTestAccess.h"

#include <gtest/gtest.h>

using namespace SputterOS;
using namespace SputterOS::SystemTests;

using Cfg = SingleCoreConfig;

class TimerRollover : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        SystemBuilder<Cfg> builder(&app, monitors, 1);
        builder.setStream(&stream).setWatchdogKick(nullptr);
        builder.core(0).addScheduledTask(&userTask);
        ASSERT_TRUE(builder.build());
        System<Cfg>::init(0);
    }

    void TearDown() override { Kernel::KernelTestAccess::resetSystem<Cfg>(); }

    InstrumentedApp<Cfg>    app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[1] = {&monitor};
    InstrumentedTask        userTask;
};

TEST_F(TimerRollover, ForwardProgressionNoFault)
{
    System<Cfg>::tick(0, SputterMicros(1000));
    System<Cfg>::tick(0, SputterMicros(2000));
    System<Cfg>::tick(0, SputterMicros(3000));

    EXPECT_EQ(System<Cfg>::errorLogger().count(), 0u);
}

TEST_F(TimerRollover, RolloverLogsFault)
{
    System<Cfg>::tick(0, SputterMicros(5000));
    System<Cfg>::tick(0, SputterMicros(1000)); // rollover: 1000 < 5000

    EXPECT_GE(System<Cfg>::errorLogger().count(), 1u);

    ErrorLogger::Entry entry{};
    bool               found = false;
    while (System<Cfg>::errorLogger().read(entry))
    {
        if (entry.code == ErrorLogger::ErrorCode::TIMER_ROLLOVER)
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected TIMER_ROLLOVER entry in ErrorLogger";
}

TEST_F(TimerRollover, RolloverEntryHasCorrectTimestamp)
{
    System<Cfg>::tick(0, SputterMicros(10000));
    System<Cfg>::tick(0, SputterMicros(500)); // rollover

    ErrorLogger::Entry entry{};
    bool               found = false;
    while (System<Cfg>::errorLogger().read(entry))
    {
        if (entry.code == ErrorLogger::ErrorCode::TIMER_ROLLOVER)
        {
            found = true;
            // The rollover is logged with the "new" (wrapped) timestamp
            EXPECT_EQ(entry.timestamp, SputterMicros(500));
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(TimerRollover, StalledClockNoSpuriousFault)
{
    System<Cfg>::tick(0, SputterMicros(5000));
    System<Cfg>::tick(0, SputterMicros(5000)); // same time — not a rollover

    EXPECT_EQ(System<Cfg>::errorLogger().count(), 0u);
}
