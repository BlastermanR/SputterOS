/**
 * @file test_DualCorePipeline.cpp
 * @brief System tests for dual-core parallel tick and cross-core command delivery.
 *
 * Uses DualCoreConfig (kCoreCount=2) and std::thread to simulate two
 * hardware cores. Verifies:
 * - Both cores can call init() concurrently without deadlock.
 * - Commands pre-loaded into the queue by "Core 1" are delivered to the
 *   IUserApplication by ControlTask running on "Core 0".
 * - Core 1 ticking (ScheduledCommsTask + background dispatch) concurrently with Core 0
 *   does not corrupt application state or crash the system.
 * - Commands pushed before concurrent ticking begins are all delivered.
 *
 * @note LockFreeQueue is SPSC-safe; the app object is only touched by
 *       ControlTask on core 0, so no additional locking is required.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "SystemTestConfig.h"
#include "SystemTestHelpers.h"

#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/System.h"

#include "../../unit/mocks/KernelTestAccess.h"

#include <atomic>
#include <gtest/gtest.h>
#include <thread>

using namespace SputterOS;
using namespace SputterOS::SystemTests;

using DCfg = DualCoreConfig;

// =========================================================================
// Fixture
// =========================================================================

class DualCorePipeline : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<DCfg>(); }
};

// =========================================================================
// Tests
// =========================================================================

TEST_F(DualCorePipeline, BothCoresInitWithoutDeadlock)
{
    InstrumentedApp<DCfg>   app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    SystemBuilder<DCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    bool core0Done = false;
    bool core1Done = false;

    std::thread core1Thread(
        [&]()
        {
            System<DCfg>::init(1);
            core1Done = true;
        });

    System<DCfg>::init(0);
    core0Done = true;

    core1Thread.join();

    EXPECT_TRUE(core0Done);
    EXPECT_TRUE(core1Done);
}

TEST_F(DualCorePipeline, CommandsPushedToQueueDeliveredByCore0)
{
    // Commands written directly to the queue (as Core 1 would via CommsTask)
    // must be consumed and delivered by Core 0's ControlTask.
    InstrumentedApp<DCfg>   app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    SystemBuilder<DCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<DCfg>::init(0);
    System<DCfg>::init(1);

    // Simulate Core 1 pushing commands before any ticks start.
    static constexpr std::size_t kCmdsToSend = 4;
    for (std::size_t i = 0; i < kCmdsToSend; ++i)
    {
        DCfg::Command cmd{
            DCfg::CmdID::SET_FLOW,
            static_cast<uint8_t>(i),
            static_cast<float>(i * 10),
        };
        System<DCfg>::commandQueue().try_push(cmd);
    }

    // Two ticks on Core 0 are sufficient to drain kCmdsToSend=4 commands
    // (kMaxCommandsPerTick=8 so all fit in a single ControlTask tick).
    System<DCfg>::tick(0, SputterMicros(1000));
    System<DCfg>::tick(0, SputterMicros(2000));

    EXPECT_EQ(app.commandCount, kCmdsToSend);
}

TEST_F(DualCorePipeline, ConcurrentTickBothCoresCompleteWithoutCrash)
{
    // Both cores tick simultaneously for several iterations and must finish
    // without deadlocks, assertion failures, or data races.
    InstrumentedApp<DCfg>   app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    SystemBuilder<DCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<DCfg>::init(0);
    System<DCfg>::init(1);

    std::atomic<bool> core1Done{false};

    std::thread core1Thread(
        [&]()
        {
            for (uint64_t t = 1000; t <= 5000; t += 1000)
            {
                System<DCfg>::tick(1, SputterMicros(t));
            }
            core1Done.store(true, std::memory_order_release);
        });

    for (uint64_t t = 1000; t <= 5000; t += 1000)
    {
        System<DCfg>::tick(0, SputterMicros(t));
    }

    core1Thread.join();
    EXPECT_TRUE(core1Done.load(std::memory_order_acquire));
}

TEST_F(DualCorePipeline, CommandsDeliveredUnderConcurrentTick)
{
    // Pre-push commands then run both cores concurrently. ControlTask on
    // Core 0 must deliver all commands despite Core 1 ticking in parallel.
    InstrumentedApp<DCfg>   app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    SystemBuilder<DCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<DCfg>::init(0);
    System<DCfg>::init(1);

    static constexpr std::size_t kCmdsToSend = 3;
    for (std::size_t i = 0; i < kCmdsToSend; ++i)
    {
        DCfg::Command cmd{DCfg::CmdID::SET_STATE, static_cast<uint8_t>(i), 0.0f};
        System<DCfg>::commandQueue().try_push(cmd);
    }

    std::thread core1Thread(
        [&]()
        {
            for (uint64_t t = 1000; t <= 5000; t += 1000)
            {
                System<DCfg>::tick(1, SputterMicros(t));
            }
        });

    for (uint64_t t = 1000; t <= 5000; t += 1000)
    {
        System<DCfg>::tick(0, SputterMicros(t));
    }

    core1Thread.join();

    EXPECT_EQ(app.commandCount, kCmdsToSend);
}
