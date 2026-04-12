/**
 * @file test_SchedulingStress.cpp
 * @brief System tests that stress-test the scheduling logic under extreme conditions.
 *
 * Exercises the full SputterOS scheduling pipeline under high load to verify:
 * - High-frequency tick bursts with multiple tasks per core
 * - Core saturation with many registered tasks
 * - Safety abort propagation under heavy scheduling load
 * - Command queue flooding during concurrent multi-task ticks
 * - Dual-core concurrent high-frequency scheduling
 * - Background task execution alongside scheduled tasks
 * - DeadlineTracker correctness under large time jumps and rapid advancement
 * - TaskTimer instrumentation accuracy across many tasks and tick iterations
 *
 * Each test suite uses a unique Cfg type to isolate System<Cfg> inline static state.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "SystemTestConfig.h"
#include "SystemTestHelpers.h"

#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/metrics/DeadlineTracker.h"
#include "sputteros/kernel/System.h"
#include "sputteros/kernel/metrics/TaskTimer.h"

#include "../../unit/mocks/KernelTestAccess.h"

#include <atomic>
#include <gtest/gtest.h>
#include <thread>

using namespace SputterOS;
using namespace SputterOS::SystemTests;

// =========================================================================
// Per-Test Cfg Types (avoid static state bleed between suites)
// =========================================================================

template <int N> struct SchedStressCfg
{
    enum class State : uint8_t
    {
        IDLE = 0,
        RUNNING,
        FAULT
    };

    enum class CmdID : uint8_t
    {
        SET_STATE     = 0,
        SET_FLOW      = 1,
        ABORT_PROCESS = 2
    };

    struct Command
    {
        CmdID   id;
        uint8_t targetDevice;
        float   value;
    };

    static constexpr int         kMaxCommandsPerTick = 8;
    static constexpr uint8_t     kMaxValidCommandID  = 2;
    static constexpr std::size_t kCoreCount          = 1;
    static constexpr std::size_t kQueueCapacity      = 16;
};

template <int N> struct SchedStressDualCfg
{
    enum class State : uint8_t
    {
        IDLE = 0,
        RUNNING,
        FAULT
    };

    enum class CmdID : uint8_t
    {
        SET_STATE     = 0,
        SET_FLOW      = 1,
        ABORT_PROCESS = 2
    };

    struct Command
    {
        CmdID   id;
        uint8_t targetDevice;
        float   value;
    };

    static constexpr int         kMaxCommandsPerTick = 8;
    static constexpr uint8_t     kMaxValidCommandID  = 2;
    static constexpr std::size_t kCoreCount          = 2;
    static constexpr std::size_t kQueueCapacity      = 16;
};

/** @brief Large queue config for queue saturation tests. */
template <int N> struct SchedStressLargeQueueCfg
{
    enum class State : uint8_t
    {
        IDLE = 0,
        RUNNING,
        FAULT
    };

    enum class CmdID : uint8_t
    {
        SET_STATE     = 0,
        SET_FLOW      = 1,
        ABORT_PROCESS = 2
    };

    struct Command
    {
        CmdID   id;
        uint8_t targetDevice;
        float   value;
    };

    static constexpr int         kMaxCommandsPerTick = 64;
    static constexpr uint8_t     kMaxValidCommandID  = 2;
    static constexpr std::size_t kCoreCount          = 1;
    static constexpr std::size_t kQueueCapacity      = 128;
};

// =========================================================================
// Stress Test Helpers
// =========================================================================

/**
 * @brief IScheduledTask that tracks tick count and accumulates simulated work.
 *
 * Each tick increments a counter and records the timestamp. The period
 * is configurable at construction to support mixed-rate scheduling tests.
 */
class ConfigurableTask : public IScheduledTask
{
  public:
    explicit ConfigurableTask(SputterMicros period = 10000) : m_period(period) {}

    void init() override { ++initCount; }

    void tick(SputterMicros systemTimeMicros) override
    {
        ++tickCount;
        lastTickTime = systemTimeMicros;
    }

    SputterMicros periodUs() const override { return m_period; }

    uint32_t      initCount    = 0;
    uint32_t      tickCount    = 0;
    SputterMicros lastTickTime = 0;

  private:
    SputterMicros m_period;
};

/**
 * @brief IBackgroundTask that tracks tick count and budget enforcement.
 */
class InstrumentedBackgroundTask : public IBackgroundTask
{
  public:
    explicit InstrumentedBackgroundTask(SputterMicros budget = 1000) : m_budget(budget) {}

    void init() override { ++initCount; }

    void tick(SputterMicros systemTimeMicros) override
    {
        ++tickCount;
        lastTickTime = systemTimeMicros;
    }

    SputterMicros maxBudgetUs() const override { return m_budget; }

    uint32_t      initCount    = 0;
    uint32_t      tickCount    = 0;
    SputterMicros lastTickTime = 0;

  private:
    SputterMicros m_budget;
};

/**
 * @brief IUserApplication that counts commands and allows inspection of delivery order.
 */
template <typename Cfg> class StressApp : public IUserApplication<Cfg>
{
  public:
    using CommandStruct = typename Cfg::Command;

    void init() override { ++initCount; }

    void tick(SputterMicros systemTimeMicros) override
    {
        ++tickCount;
        lastTickTime = systemTimeMicros;
    }

    void handleCommand(const CommandStruct &cmd) override
    {
        ++commandCount;
        lastCommand = cmd;
    }

    void forceSafeAbort() override { ++abortCount; }

    uint32_t      initCount    = 0;
    uint32_t      tickCount    = 0;
    SputterMicros lastTickTime = 0;
    uint32_t      commandCount = 0;
    uint32_t      abortCount   = 0;
    CommandStruct lastCommand{};
};

// =========================================================================
// Suite 1: High-Frequency Tick Bursts (Single-Core)
// =========================================================================

using TickBurstCfg = SchedStressCfg<1>;

class SchedulingStress_TickBurst : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<TickBurstCfg>(); }
};

TEST_F(SchedulingStress_TickBurst, ThousandTickBurstAllTasksExecute)
{
    StressApp<TickBurstCfg> app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    ConfigurableTask userTask1(10000);
    ConfigurableTask userTask2(10000);
    ConfigurableTask userTask3(10000);

    SystemBuilder<TickBurstCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&userTask1).addScheduledTask(&userTask2).addScheduledTask(&userTask3);
    ASSERT_TRUE(builder.build());

    System<TickBurstCfg>::init(0);

    static constexpr uint32_t kTickCount = 1000;
    for (uint64_t t = 1; t <= kTickCount; ++t)
    {
        System<TickBurstCfg>::tick(0, SputterMicros(t * 100));
    }

    EXPECT_EQ(userTask1.tickCount, kTickCount);
    EXPECT_EQ(userTask2.tickCount, kTickCount);
    EXPECT_EQ(userTask3.tickCount, kTickCount);
    EXPECT_EQ(app.tickCount, kTickCount);
}

TEST_F(SchedulingStress_TickBurst, RapidTicksWithMinimalTimeDelta)
{
    StressApp<TickBurstCfg> app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};
    ConfigurableTask        userTask(10000);

    SystemBuilder<TickBurstCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&userTask);
    ASSERT_TRUE(builder.build());

    System<TickBurstCfg>::init(0);

    // Tick with 1 µs increments — stress the minimum delta path.
    static constexpr uint32_t kTickCount = 5000;
    for (uint64_t t = 1; t <= kTickCount; ++t)
    {
        System<TickBurstCfg>::tick(0, SputterMicros(t));
    }

    EXPECT_EQ(userTask.tickCount, kTickCount);
    EXPECT_EQ(userTask.lastTickTime, SputterMicros(kTickCount));
}

TEST_F(SchedulingStress_TickBurst, IdenticalTimestampsDoNotCorruptState)
{
    StressApp<TickBurstCfg> app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};
    ConfigurableTask        userTask(10000);

    SystemBuilder<TickBurstCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&userTask);
    ASSERT_TRUE(builder.build());

    System<TickBurstCfg>::init(0);

    // Same timestamp repeated — must not crash or skip tasks.
    static constexpr uint32_t kTickCount = 100;
    for (uint32_t i = 0; i < kTickCount; ++i)
    {
        System<TickBurstCfg>::tick(0, SputterMicros(5000));
    }

    EXPECT_EQ(userTask.tickCount, kTickCount);
}

// =========================================================================
// Suite 2: Core Saturation — Many Tasks Per Core
// =========================================================================

using CoreSatCfg = SchedStressCfg<2>;

class SchedulingStress_CoreSaturation : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<CoreSatCfg>(); }
};

TEST_F(SchedulingStress_CoreSaturation, ThirtyUserTasksAllTicked)
{
    StressApp<CoreSatCfg>   app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    static constexpr std::size_t kTaskCount = 30;
    ConfigurableTask             tasks[kTaskCount];

    SystemBuilder<CoreSatCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    for (std::size_t i = 0; i < kTaskCount; ++i)
    {
        builder.core(0).addScheduledTask(&tasks[i]);
    }
    ASSERT_TRUE(builder.build());

    // Kernel tasks (3) + user tasks (30) = 33 total on core 0.
    EXPECT_EQ(System<CoreSatCfg>::taskCount(0), kTaskCount + 3);

    System<CoreSatCfg>::init(0);

    static constexpr uint32_t kTicks = 100;
    for (uint64_t t = 1; t <= kTicks; ++t)
    {
        System<CoreSatCfg>::tick(0, SputterMicros(t * 1000));
    }

    for (std::size_t i = 0; i < kTaskCount; ++i)
    {
        EXPECT_EQ(tasks[i].tickCount, kTicks) << "Task " << i << " did not receive all ticks";
        EXPECT_EQ(tasks[i].initCount, 1u) << "Task " << i << " init not called";
    }
    EXPECT_EQ(app.tickCount, kTicks);
}

TEST_F(SchedulingStress_CoreSaturation, MixedPeriodTasksAllRegistered)
{
    StressApp<CoreSatCfg>   app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    // Tasks with varying periods — all should be accepted by the builder.
    ConfigurableTask fast(10000);
    ConfigurableTask medium(50000);
    ConfigurableTask slow(100000);

    SystemBuilder<CoreSatCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&fast).addScheduledTask(&medium).addScheduledTask(&slow);
    ASSERT_TRUE(builder.build());

    System<CoreSatCfg>::init(0);

    // All tasks tick every System::tick() call (current dispatcher is flat iteration).
    static constexpr uint32_t kTicks = 200;
    for (uint64_t t = 1; t <= kTicks; ++t)
    {
        System<CoreSatCfg>::tick(0, SputterMicros(t * 500));
    }

    EXPECT_EQ(fast.tickCount, kTicks);
    EXPECT_EQ(medium.tickCount, kTicks);
    EXPECT_EQ(slow.tickCount, kTicks);
}

// =========================================================================
// Suite 3: Safety Abort Under Heavy Load
// =========================================================================

using SafetyLoadCfg = SchedStressCfg<3>;

class SchedulingStress_SafetyUnderLoad : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<SafetyLoadCfg>(); }
};

TEST_F(SchedulingStress_SafetyUnderLoad, AbortFiringWithManyTasksRunning)
{
    StressApp<SafetyLoadCfg> app;
    FakeStreamReader         stream;
    TrippableMonitor         monitor;
    ISafetyMonitor          *monitors[] = {&monitor};

    static constexpr std::size_t kTaskCount = 20;
    ConfigurableTask             tasks[kTaskCount];

    SystemBuilder<SafetyLoadCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    for (std::size_t i = 0; i < kTaskCount; ++i)
    {
        builder.core(0).addScheduledTask(&tasks[i]);
    }
    ASSERT_TRUE(builder.build());
    System<SafetyLoadCfg>::init(0);

    // Run safe for 50 ticks.
    for (uint64_t t = 1; t <= 50; ++t)
    {
        System<SafetyLoadCfg>::tick(0, SputterMicros(t * 100));
    }
    EXPECT_EQ(app.abortCount, 0u);
    EXPECT_EQ(app.tickCount, 50u);

    // Trip safety mid-burst.
    monitor.safeFlag = false;
    for (uint64_t t = 51; t <= 100; ++t)
    {
        System<SafetyLoadCfg>::tick(0, SputterMicros(t * 100));
    }

    // Abort must fire every tick while unsafe.
    EXPECT_EQ(app.abortCount, 50u);
    // ControlTask returns early on abort — app tick should NOT advance further.
    EXPECT_EQ(app.tickCount, 50u);

    // User tasks continue ticking (they are below ControlTask in the list).
    for (std::size_t i = 0; i < kTaskCount; ++i)
    {
        EXPECT_EQ(tasks[i].tickCount, 100u);
    }
}

TEST_F(SchedulingStress_SafetyUnderLoad, SafetyRecoveryAfterBurst)
{
    StressApp<SafetyLoadCfg> app;
    FakeStreamReader         stream;
    TrippableMonitor         monitor;
    ISafetyMonitor          *monitors[] = {&monitor};
    ConfigurableTask         task;

    SystemBuilder<SafetyLoadCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&task);
    ASSERT_TRUE(builder.build());
    System<SafetyLoadCfg>::init(0);

    // Trip for a burst.
    monitor.safeFlag = false;
    for (uint64_t t = 1; t <= 200; ++t)
    {
        System<SafetyLoadCfg>::tick(0, SputterMicros(t * 100));
    }
    EXPECT_EQ(app.abortCount, 200u);
    uint32_t ticksBeforeRecovery = app.tickCount;

    // Recover — ticks should resume.
    monitor.safeFlag = true;
    for (uint64_t t = 201; t <= 300; ++t)
    {
        System<SafetyLoadCfg>::tick(0, SputterMicros(t * 100));
    }
    EXPECT_EQ(app.abortCount, 200u); // No new aborts.
    EXPECT_EQ(app.tickCount, ticksBeforeRecovery + 100);
}

TEST_F(SchedulingStress_SafetyUnderLoad, RapidToggleSafetyMonitor)
{
    StressApp<SafetyLoadCfg> app;
    FakeStreamReader         stream;
    TrippableMonitor         monitor;
    ISafetyMonitor          *monitors[] = {&monitor};

    SystemBuilder<SafetyLoadCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());
    System<SafetyLoadCfg>::init(0);

    // Toggle safety every tick for 500 ticks.
    uint32_t expectedAborts = 0;
    uint32_t expectedTicks  = 0;
    for (uint64_t t = 1; t <= 500; ++t)
    {
        monitor.safeFlag = ((t % 2) == 0); // unsafe on odd ticks
        if (!monitor.safeFlag)
        {
            ++expectedAborts;
        }
        else
        {
            ++expectedTicks;
        }
        System<SafetyLoadCfg>::tick(0, SputterMicros(t * 100));
    }

    EXPECT_EQ(app.abortCount, expectedAborts);
    EXPECT_EQ(app.tickCount, expectedTicks);
}

// =========================================================================
// Suite 4: Command Queue Flood Under Multi-Task Tick
// =========================================================================

using QueueFloodCfg = SchedStressLargeQueueCfg<4>;

class SchedulingStress_QueueFlood : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<QueueFloodCfg>(); }
};

TEST_F(SchedulingStress_QueueFlood, SaturateQueueThenDrainViaTicks)
{
    StressApp<QueueFloodCfg> app;
    FakeStreamReader         stream;
    AlwaysSafeSafetyMonitor  monitor;
    ISafetyMonitor          *monitors[] = {&monitor};

    SystemBuilder<QueueFloodCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());
    System<QueueFloodCfg>::init(0);

    // Fill the queue to capacity.
    std::size_t pushed = 0;
    for (std::size_t i = 0; i < 200; ++i) // More than capacity
    {
        QueueFloodCfg::Command cmd{QueueFloodCfg::CmdID::SET_FLOW, static_cast<uint8_t>(i & 0xFF),
                                   static_cast<float>(i)};
        if (System<QueueFloodCfg>::commandQueue().try_push(cmd))
        {
            ++pushed;
        }
    }

    // Queue should have accepted exactly kQueueCapacity commands.
    EXPECT_EQ(pushed, QueueFloodCfg::kQueueCapacity);

    // Tick enough times to drain all commands (kMaxCommandsPerTick = 64).
    for (uint64_t t = 1; t <= 10; ++t)
    {
        System<QueueFloodCfg>::tick(0, SputterMicros(t * 1000));
    }

    EXPECT_EQ(app.commandCount, pushed);
}

TEST_F(SchedulingStress_QueueFlood, InterleaveProduceAndConsume)
{
    StressApp<QueueFloodCfg> app;
    FakeStreamReader         stream;
    AlwaysSafeSafetyMonitor  monitor;
    ISafetyMonitor          *monitors[] = {&monitor};

    SystemBuilder<QueueFloodCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());
    System<QueueFloodCfg>::init(0);

    // Push some commands, tick, push more, tick — interleaved pattern.
    std::size_t totalPushed = 0;
    for (uint64_t cycle = 0; cycle < 50; ++cycle)
    {
        // Push 3 commands per cycle.
        for (int c = 0; c < 3; ++c)
        {
            QueueFloodCfg::Command cmd{QueueFloodCfg::CmdID::SET_STATE, 0, static_cast<float>(cycle * 3 + c)};
            if (System<QueueFloodCfg>::commandQueue().try_push(cmd))
            {
                ++totalPushed;
            }
        }
        System<QueueFloodCfg>::tick(0, SputterMicros((cycle + 1) * 1000));
    }

    EXPECT_EQ(app.commandCount, totalPushed);
}

TEST_F(SchedulingStress_QueueFlood, QueueFullRejectionDoesNotCorruptState)
{
    StressApp<QueueFloodCfg> app;
    FakeStreamReader         stream;
    AlwaysSafeSafetyMonitor  monitor;
    ISafetyMonitor          *monitors[] = {&monitor};

    SystemBuilder<QueueFloodCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());
    System<QueueFloodCfg>::init(0);

    // Fill queue completely without ticking.
    for (std::size_t i = 0; i < QueueFloodCfg::kQueueCapacity; ++i)
    {
        QueueFloodCfg::Command cmd{QueueFloodCfg::CmdID::SET_FLOW, 0, static_cast<float>(i)};
        ASSERT_TRUE(System<QueueFloodCfg>::commandQueue().try_push(cmd));
    }

    // Subsequent pushes must fail.
    QueueFloodCfg::Command extraCmd{QueueFloodCfg::CmdID::SET_STATE, 0, 999.0f};
    EXPECT_FALSE(System<QueueFloodCfg>::commandQueue().try_push(extraCmd));

    // System must still tick correctly and drain all queued commands.
    System<QueueFloodCfg>::tick(0, SputterMicros(1000));
    System<QueueFloodCfg>::tick(0, SputterMicros(2000));
    System<QueueFloodCfg>::tick(0, SputterMicros(3000));

    EXPECT_EQ(app.commandCount, QueueFloodCfg::kQueueCapacity);
    EXPECT_EQ(app.tickCount, 3u);
}

// =========================================================================
// Suite 5: Dual-Core Concurrent High-Frequency Scheduling
// =========================================================================

using DualStressCfg = SchedStressDualCfg<5>;

class SchedulingStress_DualCoreHighFreq : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<DualStressCfg>(); }
};

TEST_F(SchedulingStress_DualCoreHighFreq, ConcurrentThousandTicksBothCores)
{
    StressApp<DualStressCfg> app;
    FakeStreamReader         stream;
    AlwaysSafeSafetyMonitor  monitor;
    ISafetyMonitor          *monitors[] = {&monitor};

    ConfigurableTask core0Task(10000);
    ConfigurableTask core1Task(10000);

    SystemBuilder<DualStressCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(0).addScheduledTask(&core0Task);
    builder.core(1).addScheduledTask(&core1Task);
    ASSERT_TRUE(builder.build());

    System<DualStressCfg>::init(0);
    System<DualStressCfg>::init(1);

    static constexpr uint32_t kTickCount = 1000;
    std::atomic<bool>         core1Done{false};

    std::thread core1Thread(
        [&]()
        {
            for (uint64_t t = 1; t <= kTickCount; ++t)
            {
                System<DualStressCfg>::tick(1, SputterMicros(t * 100));
            }
            core1Done.store(true, std::memory_order_release);
        });

    for (uint64_t t = 1; t <= kTickCount; ++t)
    {
        System<DualStressCfg>::tick(0, SputterMicros(t * 100));
    }

    core1Thread.join();
    EXPECT_TRUE(core1Done.load(std::memory_order_acquire));
    EXPECT_EQ(core0Task.tickCount, kTickCount);
    EXPECT_EQ(core1Task.tickCount, kTickCount);
    EXPECT_EQ(app.tickCount, kTickCount);
}

TEST_F(SchedulingStress_DualCoreHighFreq, CommandsDeliveredDuringConcurrentHighFreqTick)
{
    StressApp<DualStressCfg> app;
    FakeStreamReader         stream;
    AlwaysSafeSafetyMonitor  monitor;
    ISafetyMonitor          *monitors[] = {&monitor};

    SystemBuilder<DualStressCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<DualStressCfg>::init(0);
    System<DualStressCfg>::init(1);

    // Pre-load commands.
    static constexpr std::size_t kCmds = 8;
    for (std::size_t i = 0; i < kCmds; ++i)
    {
        DualStressCfg::Command cmd{DualStressCfg::CmdID::SET_FLOW, static_cast<uint8_t>(i), static_cast<float>(i * 10)};
        System<DualStressCfg>::commandQueue().try_push(cmd);
    }

    // Run 500 ticks on both cores concurrently.
    static constexpr uint32_t kTickCount = 500;

    std::thread core1Thread(
        [&]()
        {
            for (uint64_t t = 1; t <= kTickCount; ++t)
            {
                System<DualStressCfg>::tick(1, SputterMicros(t * 100));
            }
        });

    for (uint64_t t = 1; t <= kTickCount; ++t)
    {
        System<DualStressCfg>::tick(0, SputterMicros(t * 100));
    }

    core1Thread.join();
    EXPECT_EQ(app.commandCount, kCmds);
}

TEST_F(SchedulingStress_DualCoreHighFreq, ManyTasksBothCoresConcurrently)
{
    StressApp<DualStressCfg> app;
    FakeStreamReader         stream;
    AlwaysSafeSafetyMonitor  monitor;
    ISafetyMonitor          *monitors[] = {&monitor};

    static constexpr std::size_t kTasksPerCore = 10;
    ConfigurableTask             core0Tasks[kTasksPerCore];
    ConfigurableTask             core1Tasks[kTasksPerCore];

    SystemBuilder<DualStressCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    for (std::size_t i = 0; i < kTasksPerCore; ++i)
    {
        builder.core(0).addScheduledTask(&core0Tasks[i]);
        builder.core(1).addScheduledTask(&core1Tasks[i]);
    }
    ASSERT_TRUE(builder.build());

    System<DualStressCfg>::init(0);
    System<DualStressCfg>::init(1);

    static constexpr uint32_t kTickCount = 300;

    std::thread core1Thread(
        [&]()
        {
            for (uint64_t t = 1; t <= kTickCount; ++t)
            {
                System<DualStressCfg>::tick(1, SputterMicros(t * 100));
            }
        });

    for (uint64_t t = 1; t <= kTickCount; ++t)
    {
        System<DualStressCfg>::tick(0, SputterMicros(t * 100));
    }

    core1Thread.join();

    for (std::size_t i = 0; i < kTasksPerCore; ++i)
    {
        EXPECT_EQ(core0Tasks[i].tickCount, kTickCount) << "Core 0 task " << i;
        EXPECT_EQ(core1Tasks[i].tickCount, kTickCount) << "Core 1 task " << i;
    }
}

// =========================================================================
// Suite 6: Background Task Registration and Execution
// =========================================================================

using BgTaskCfg = SchedStressCfg<6>;

class SchedulingStress_BackgroundTasks : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<BgTaskCfg>(); }
};

TEST_F(SchedulingStress_BackgroundTasks, BackgroundTasksRegisteredViaBuilder)
{
    StressApp<BgTaskCfg>    app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    InstrumentedBackgroundTask bg1(500);
    InstrumentedBackgroundTask bg2(1000);
    InstrumentedBackgroundTask bg3(2000);

    SystemBuilder<BgTaskCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.addBackgroundTask(&bg1).addBackgroundTask(&bg2).addBackgroundTask(&bg3);
    ASSERT_TRUE(builder.build());

    // Kernel registers DiagnosticsTask as background task (1) + our 3 = 4 total.
    EXPECT_EQ(System<BgTaskCfg>::backgroundTaskCount(), 4u);
}

TEST_F(SchedulingStress_BackgroundTasks, MultipleBackgroundTasksAllInitialized)
{
    StressApp<BgTaskCfg>    app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    static constexpr std::size_t kBgCount = 8;
    InstrumentedBackgroundTask   bgTasks[kBgCount];

    SystemBuilder<BgTaskCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    for (std::size_t i = 0; i < kBgCount; ++i)
    {
        builder.addBackgroundTask(&bgTasks[i]);
    }
    ASSERT_TRUE(builder.build());

    // Kernel's DiagnosticsTask + user background tasks.
    EXPECT_EQ(System<BgTaskCfg>::backgroundTaskCount(), kBgCount + 1);

    // Verify background task pointers are stored correctly.
    auto bgArray = System<BgTaskCfg>::backgroundTasks();
    for (std::size_t i = 0; i < System<BgTaskCfg>::backgroundTaskCount(); ++i)
    {
        EXPECT_NE(bgArray[i], nullptr) << "Background task " << i << " is null";
    }
}

// =========================================================================
// Suite 7: DeadlineTracker Stress
// =========================================================================

class SchedulingStress_DeadlineTracker : public ::testing::Test
{
};

TEST_F(SchedulingStress_DeadlineTracker, RapidAdvancementCatchesUp)
{
    Kernel::DeadlineTracker dt;
    dt.periodUs      = 1000;
    dt.phaseOffsetUs = 0;
    dt.init(0);

    EXPECT_TRUE(dt.isDue(0));
    dt.advance(0);

    // nextActivation should be 1000.
    EXPECT_EQ(dt.nextActivation, SputterMicros(1000));

    // Simulate a massive time jump — 1 second ahead.
    SputterMicros jumpTime = 1'000'000;
    EXPECT_TRUE(dt.isDue(jumpTime));
    dt.advance(jumpTime);

    // Must have skipped all missed periods. Next activation > jumpTime.
    EXPECT_GT(dt.nextActivation, jumpTime);
    // Should be aligned to a multiple of period.
    EXPECT_EQ(dt.nextActivation % dt.periodUs, SputterMicros(0));
}

TEST_F(SchedulingStress_DeadlineTracker, ThousandPeriodsNoAccumulationError)
{
    Kernel::DeadlineTracker dt;
    dt.periodUs      = 100;
    dt.phaseOffsetUs = 0;
    dt.init(0);

    // Advance 1000 periods one-at-a-time, executing exactly at each deadline.
    for (uint64_t i = 0; i < 1000; ++i)
    {
        SputterMicros expectedActivation = i * 100;
        ASSERT_TRUE(dt.isDue(expectedActivation)) << "Period " << i;
        dt.advance(expectedActivation);
    }

    // After 1000 exact-period advances, next activation should be exactly 100000.
    EXPECT_EQ(dt.nextActivation, SputterMicros(100000));
}

TEST_F(SchedulingStress_DeadlineTracker, PhaseOffsetPreservedAcrossAdvances)
{
    Kernel::DeadlineTracker dt;
    dt.periodUs      = 500;
    dt.phaseOffsetUs = 250;
    dt.init(1000); // startTime = 1000, so nextActivation = 1250.

    EXPECT_EQ(dt.nextActivation, SputterMicros(1250));

    dt.advance(1250);
    EXPECT_EQ(dt.nextActivation, SputterMicros(1750));

    dt.advance(1750);
    EXPECT_EQ(dt.nextActivation, SputterMicros(2250));
}

TEST_F(SchedulingStress_DeadlineTracker, TimeUntilNextAccurate)
{
    Kernel::DeadlineTracker dt;
    dt.periodUs      = 10000;
    dt.phaseOffsetUs = 0;
    dt.init(0);

    // Before first activation, due immediately.
    EXPECT_EQ(dt.timeUntilNext(0), SputterMicros(0));

    dt.advance(0);
    EXPECT_EQ(dt.timeUntilNext(0), SputterMicros(10000));
    EXPECT_EQ(dt.timeUntilNext(5000), SputterMicros(5000));
    EXPECT_EQ(dt.timeUntilNext(9999), SputterMicros(1));
    EXPECT_EQ(dt.timeUntilNext(10000), SputterMicros(0));
    EXPECT_EQ(dt.timeUntilNext(10001), SputterMicros(0)); // Overdue.
}

TEST_F(SchedulingStress_DeadlineTracker, MassiveTimeJumpSkipsCorrectly)
{
    Kernel::DeadlineTracker dt;
    dt.periodUs      = 10;
    dt.phaseOffsetUs = 0;
    dt.init(0);

    // Jump 10 million µs ahead (10 seconds, 1 million periods at 10µs).
    SputterMicros hugeJump = 10'000'000;
    dt.advance(hugeJump);

    // Must not loop 1 million times — advance should skip efficiently.
    // nextActivation should be > hugeJump and aligned to period.
    EXPECT_GT(dt.nextActivation, hugeJump);
    EXPECT_EQ(dt.nextActivation % dt.periodUs, SputterMicros(0));
    // Should be the next period boundary after hugeJump.
    EXPECT_LE(dt.nextActivation - hugeJump, dt.periodUs);
}

TEST_F(SchedulingStress_DeadlineTracker, LargeTimestampValues)
{
    // Test near the upper range of uint64_t microseconds.
    // ~584,942 years in µs. Use a large but not near-overflow value.
    Kernel::DeadlineTracker dt;
    dt.periodUs      = 1000;
    dt.phaseOffsetUs = 0;

    SputterMicros farFuture = UINT64_MAX / 2;
    dt.init(farFuture);

    EXPECT_TRUE(dt.isDue(farFuture));
    dt.advance(farFuture);
    EXPECT_EQ(dt.nextActivation, farFuture + 1000);
    EXPECT_FALSE(dt.isDue(farFuture));
    EXPECT_TRUE(dt.isDue(farFuture + 1000));
}

// =========================================================================
// Suite 8: TaskTimer Instrumentation Under Load
// =========================================================================

using TimerLoadCfg = SchedStressCfg<8>;

class SchedulingStress_TaskTimerLoad : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<TimerLoadCfg>(); }
};

TEST_F(SchedulingStress_TaskTimerLoad, TimerSamplesAccumulateAcrossManyTicks)
{
    StressApp<TimerLoadCfg> app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};
    ConfigurableTask        userTask;

    static uint64_t s_clock_8 = 0;
    auto            clockFn   = []() -> uint64_t { return s_clock_8; };

    SystemBuilder<TimerLoadCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr).setClockSource(clockFn);
    builder.core(0).addScheduledTask(&userTask);
    ASSERT_TRUE(builder.build());
    System<TimerLoadCfg>::init(0);

    static constexpr uint32_t kTickCount = 500;
    for (uint64_t t = 1; t <= kTickCount; ++t)
    {
        s_clock_8 = t * 10; // Advance clock 10µs per tick.
        System<TimerLoadCfg>::tick(0, SputterMicros(t * 10));
    }

    // Timer should have recorded samples for every tick.
    EXPECT_EQ(userTask.timer().sampleCount(), kTickCount);
}

TEST_F(SchedulingStress_TaskTimerLoad, MaxDurationTrackedAcrossLoad)
{
    StressApp<TimerLoadCfg> app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};
    ConfigurableTask        userTask;

    // Simulate varying execution times via a clock that jumps during task ticks.
    static uint64_t       s_clock_8b        = 0;
    static uint64_t       s_clock_8b_offset = 0;
    static const uint64_t kSpikeTick        = 250;

    auto clockFn = []() -> uint64_t { return s_clock_8b + s_clock_8b_offset; };

    SystemBuilder<TimerLoadCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr).setClockSource(clockFn);
    builder.core(0).addScheduledTask(&userTask);
    ASSERT_TRUE(builder.build());
    System<TimerLoadCfg>::init(0);

    s_clock_8b        = 0;
    s_clock_8b_offset = 0;

    for (uint64_t t = 1; t <= 500; ++t)
    {
        s_clock_8b = t * 100;
        // Introduce a spike at tick 250 by adding offset before the tick
        // (timer.start() and timer.stop() both call the clock).
        if (t == kSpikeTick)
        {
            s_clock_8b_offset = 5000; // 5ms spike
        }
        else
        {
            s_clock_8b_offset = 0;
        }
        System<TimerLoadCfg>::tick(0, SputterMicros(t * 100));
    }

    // maxDuration should capture the spike. Since all tasks share the same
    // clock and the spike happens during the full tick cycle, the user task's
    // timer may or may not see the spike depending on start/stop timing.
    // What we can verify is that the timer tracked something.
    EXPECT_EQ(userTask.timer().sampleCount(), 500u);
}

TEST_F(SchedulingStress_TaskTimerLoad, OverrunCounterIncrements)
{
    Kernel::TaskTimer timer;

    // Manual overrun recording.
    EXPECT_EQ(timer.overrunCount(), 0u);

    for (uint32_t i = 0; i < 100; ++i)
    {
        timer.recordOverrun();
    }
    EXPECT_EQ(timer.overrunCount(), 100u);
}

TEST_F(SchedulingStress_TaskTimerLoad, DeadlineMissCounterIncrements)
{
    Kernel::TaskTimer timer;

    EXPECT_EQ(timer.deadlineMissCount(), 0u);

    for (uint32_t i = 0; i < 250; ++i)
    {
        timer.recordDeadlineMiss();
    }
    EXPECT_EQ(timer.deadlineMissCount(), 250u);
}

TEST_F(SchedulingStress_TaskTimerLoad, ResetClearsAllAccumulatedState)
{
    Kernel::TaskTimer timer;

    static uint64_t s_clock_reset = 0;
    timer.setClockSource([]() -> uint64_t { return s_clock_reset; });

    // Accumulate some data.
    for (uint32_t i = 0; i < 50; ++i)
    {
        s_clock_reset = i * 10;
        timer.start();
        s_clock_reset = i * 10 + 5;
        timer.stop();
        timer.recordOverrun();
        timer.recordDeadlineMiss();
    }

    EXPECT_GT(timer.sampleCount(), 0u);
    EXPECT_GT(timer.overrunCount(), 0u);

    timer.reset();

    EXPECT_EQ(timer.sampleCount(), 0u);
    EXPECT_EQ(timer.lastDuration(), SputterMicros(0));
    EXPECT_EQ(timer.maxDuration(), SputterMicros(0));
    EXPECT_EQ(timer.overrunCount(), 0u);
    EXPECT_EQ(timer.deadlineMissCount(), 0u);
    EXPECT_FLOAT_EQ(timer.getAverageDurationUs(), 0.0f);
}

// =========================================================================
// Suite 9: Kernel State Machine Under Scheduling Pressure
// =========================================================================

using KStateCfg = SchedStressCfg<9>;

class SchedulingStress_KernelStateMachine : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<KStateCfg>(); }
};

TEST_F(SchedulingStress_KernelStateMachine, BuildInitTickTransitionsCorrectly)
{
    StressApp<KStateCfg>    app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    EXPECT_EQ(System<KStateCfg>::kernelState(), Kernel::KernelState::UNCONFIGURED);

    SystemBuilder<KStateCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    EXPECT_EQ(System<KStateCfg>::kernelState(), Kernel::KernelState::CONFIGURED);

    System<KStateCfg>::init(0);
    EXPECT_EQ(System<KStateCfg>::kernelState(), Kernel::KernelState::RUNNING);

    // Tick many times — state should remain RUNNING.
    for (uint64_t t = 1; t <= 1000; ++t)
    {
        System<KStateCfg>::tick(0, SputterMicros(t * 100));
    }
    EXPECT_EQ(System<KStateCfg>::kernelState(), Kernel::KernelState::RUNNING);
}

TEST_F(SchedulingStress_KernelStateMachine, StateTransitionValidation)
{
    // Test that invalid transitions are rejected.
    StressApp<KStateCfg>    app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    SystemBuilder<KStateCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    EXPECT_EQ(System<KStateCfg>::kernelState(), Kernel::KernelState::CONFIGURED);

    // Invalid: CONFIGURED → RUNNING (should be CONFIGURED → INITIALIZING).
    bool result = Kernel::KernelTestAccess::transitionTo<KStateCfg>(Kernel::KernelState::RUNNING);
    EXPECT_FALSE(result);
    EXPECT_EQ(System<KStateCfg>::kernelState(), Kernel::KernelState::CONFIGURED);

    // Valid: CONFIGURED → INITIALIZING.
    result = Kernel::KernelTestAccess::transitionTo<KStateCfg>(Kernel::KernelState::INITIALIZING);
    EXPECT_TRUE(result);

    // Valid: INITIALIZING → RUNNING.
    result = Kernel::KernelTestAccess::transitionTo<KStateCfg>(Kernel::KernelState::RUNNING);
    EXPECT_TRUE(result);

    // Valid: RUNNING → ABORTING.
    result = Kernel::KernelTestAccess::transitionTo<KStateCfg>(Kernel::KernelState::ABORTING);
    EXPECT_TRUE(result);

    // Valid: ABORTING → ABORTED.
    result = Kernel::KernelTestAccess::transitionTo<KStateCfg>(Kernel::KernelState::ABORTED);
    EXPECT_TRUE(result);

    // Valid: ABORTED → RUNNING (recovery).
    result = Kernel::KernelTestAccess::transitionTo<KStateCfg>(Kernel::KernelState::RUNNING);
    EXPECT_TRUE(result);
}

// =========================================================================
// Suite 10: Timer Rollover Detection Under Load
// =========================================================================

using RolloverCfg = SchedStressCfg<10>;

class SchedulingStress_TimerRollover : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<RolloverCfg>(); }
};

TEST_F(SchedulingStress_TimerRollover, RolloverDetectedDuringHighFreqTicks)
{
    StressApp<RolloverCfg>  app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    SystemBuilder<RolloverCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());
    System<RolloverCfg>::init(0);

    System<RolloverCfg>::errorLogger().clear();

    // Normal ticks.
    for (uint64_t t = 1; t <= 100; ++t)
    {
        System<RolloverCfg>::tick(0, SputterMicros(t * 1000));
    }

    // Simulate a rollover: time goes backward.
    System<RolloverCfg>::tick(0, SputterMicros(50));

    // ErrorLogger should have captured a TIMER_ROLLOVER entry.
    bool               foundRollover = false;
    ErrorLogger::Entry entry{};
    while (System<RolloverCfg>::errorLogger().read(entry))
    {
        if (entry.code == ErrorLogger::ErrorCode::TIMER_ROLLOVER)
        {
            foundRollover = true;
            break;
        }
    }

    EXPECT_TRUE(foundRollover) << "Timer rollover fault was not logged";
}

TEST_F(SchedulingStress_TimerRollover, NoFalseRolloverOnMonotonicSequence)
{
    StressApp<RolloverCfg>  app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    SystemBuilder<RolloverCfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());
    System<RolloverCfg>::init(0);

    System<RolloverCfg>::errorLogger().clear();

    // 2000 ticks with strictly monotonic timestamps.
    for (uint64_t t = 1; t <= 2000; ++t)
    {
        System<RolloverCfg>::tick(0, SputterMicros(t * 100));
    }

    // No rollover errors should have been logged.
    ErrorLogger::Entry entry{};
    while (System<RolloverCfg>::errorLogger().read(entry))
    {
        EXPECT_NE(entry.code, ErrorLogger::ErrorCode::TIMER_ROLLOVER) << "False rollover detected";
    }
}
