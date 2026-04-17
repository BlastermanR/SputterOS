/**
 * @file test_MultiCoreSync.cpp
 * @brief System tests for multi-core synchronization barriers.
 *
 * Tests MultiCoreSync through the System<DualCoreConfig> accessor to verify:
 * - Startup barrier succeeds when both cores signal ready
 * - Timeout triggers ERROR state
 * - Shutdown barrier drains when both cores signal
 * - Core state queries reflect transitions
 */

#include "SystemTestConfig.h"
#include "SystemTestHelpers.h"

#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/System.h"
#include "sputteros/osal/sync/MultiCoreSync.h"

#include "../../unit/mocks/KernelTestAccess.h"

#include <gtest/gtest.h>
#include <thread>

using namespace SputterOS;
using namespace SputterOS::SystemTests;

using DCfg = DualCoreConfig;

class MultiCoreSyncTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        InstrumentedApp<DCfg>   app;
        FakeStreamReader        stream;
        AlwaysSafeSafetyMonitor monitor;
        ISafetyMonitor         *monitors[] = {&monitor};

        SystemBuilder<DCfg> builder(&app, monitors, 1);
        builder.setStream(&stream).setWatchdogKick(nullptr);
        ASSERT_TRUE(builder.build());
    }

    void TearDown() override { Kernel::KernelTestAccess::resetSystem<DCfg>(); }
};

TEST_F(MultiCoreSyncTest, StartupBarrierSucceeds)
{
    auto &sync = System<DCfg>::multiCoreSync();

    // Simulate two cores entering the startup barrier from separate threads.
    bool core0Ok = false;
    bool core1Ok = false;

    std::thread core1Thread([&]() { core1Ok = sync.startupBarrier(1, 2000); });

    // Core 0 enters the barrier — with both cores entering, they should both proceed.
    core0Ok = sync.startupBarrier(0, 2000);

    core1Thread.join();

    EXPECT_TRUE(core0Ok) << "Core 0 should pass startup barrier";
    EXPECT_TRUE(core1Ok) << "Core 1 should pass startup barrier";
}

TEST_F(MultiCoreSyncTest, StartupBarrierTimeoutSetsError)
{
    auto &sync = System<DCfg>::multiCoreSync();

    // Only Core 0 enters the barrier — Core 1 never arrives.
    // Use a very short timeout to avoid blocking the test suite.
    bool core0Ok = sync.startupBarrier(0, 5, 1);

    EXPECT_FALSE(core0Ok) << "Should timeout when peer never arrives";
    EXPECT_EQ(sync.coreState(0), CoreState::ERROR);
}

TEST_F(MultiCoreSyncTest, ShutdownBarrierSucceeds)
{
    auto &sync = System<DCfg>::multiCoreSync();

    // Manually set both cores to READY (bypassing startup barrier timing).
    sync.setReady(0);
    sync.setReady(1);
    ASSERT_TRUE(sync.allReady());

    // Now test the shutdown barrier with both cores cooperating.
    bool core0ShutOk = false;
    bool core1ShutOk = false;

    std::thread core1Thread([&]() { core1ShutOk = sync.shutdownBarrier(1, 2000); });

    core0ShutOk = sync.shutdownBarrier(0, 2000);

    core1Thread.join();

    EXPECT_TRUE(core0ShutOk) << "Core 0 should pass shutdown barrier";
    EXPECT_TRUE(core1ShutOk) << "Core 1 should pass shutdown barrier";
    EXPECT_TRUE(sync.allTerminated());
}

TEST_F(MultiCoreSyncTest, CoreStateTransitions)
{
    auto &sync = System<DCfg>::multiCoreSync();

    // Initially UNBORN
    EXPECT_EQ(sync.coreState(0), CoreState::UNBORN);
    EXPECT_EQ(sync.coreState(1), CoreState::UNBORN);

    sync.setInit(0);
    EXPECT_EQ(sync.coreState(0), CoreState::INIT);

    sync.setError(0, "test error");
    EXPECT_EQ(sync.coreState(0), CoreState::ERROR);
    EXPECT_TRUE(sync.anyError());
}
