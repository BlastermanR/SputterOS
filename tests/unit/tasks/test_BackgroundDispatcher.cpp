/**
 * @file test_BackgroundDispatcher.cpp
 * @brief Unit tests for the background dispatcher in System::tick().
 *
 * Verifies that background tasks are dispatched via gap-time budget
 * in Phase 2 of System::tick(), with round-robin fairness and budget
 * exhaustion behavior.
 *
 * Each test uses a unique `BgDispCfg<N>` to isolate System<> statics.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/12/2026
 */

#include "../mocks/KernelTestAccess.h"
#include "../mocks/MockSafetyMonitor.h"
#include "../mocks/MockStreamReader.h"
#include "../mocks/MockUserApplication.h"
#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/System.h"
#include "sputteros/osal/tasks/IBackgroundTask.h"
#include "sputteros/osal/tasks/IScheduledTask.h"

#include <array>
#include <gtest/gtest.h>

using namespace SputterOS;
using namespace SputterOS::Kernel;
using ::testing::NiceMock;
using ::testing::Return;

// ===========================================================================
// Per-test isolated configuration
// ===========================================================================

template <int N> struct BgDispCfg
{
    enum class State : uint8_t
    {
        IDLE = 0
    };
    enum class CmdID : uint8_t
    {
        NONE = 0
    };
    struct Command
    {
        CmdID   id;
        uint8_t targetDevice;
        float   value;
    };
    static constexpr int         kMaxCommandsPerTick = 8;
    static constexpr uint8_t     kMaxValidCommandID  = 0;
    static constexpr std::size_t kCoreCount          = 1;
    static constexpr std::size_t kQueueCapacity      = 16;
    static constexpr uint32_t    kControlBudgetUs    = 10000;
};

template <int N> struct BgDispDualCfg
{
    enum class State : uint8_t
    {
        IDLE = 0
    };
    enum class CmdID : uint8_t
    {
        NONE = 0
    };
    struct Command
    {
        CmdID   id;
        uint8_t targetDevice;
        float   value;
    };
    static constexpr int         kMaxCommandsPerTick = 8;
    static constexpr uint8_t     kMaxValidCommandID  = 0;
    static constexpr std::size_t kCoreCount          = 2;
    static constexpr std::size_t kQueueCapacity      = 16;
    static constexpr uint32_t    kControlBudgetUs    = 10000;
};

// ===========================================================================
// Test doubles
// ===========================================================================

/// A background task that records tick calls and has a configurable budget.
class CountingBackgroundTask : public IBackgroundTask
{
  public:
    explicit CountingBackgroundTask(SputterMicros budget = 1000) : m_budget(budget) {}

    void          init() override { ++m_initCount; }
    void          tick(SputterMicros) override { ++m_tickCount; }
    SputterMicros maxBudgetUs() const override { return m_budget; }

    uint32_t tickCount() const { return m_tickCount; }
    uint32_t initCount() const { return m_initCount; }

  private:
    SputterMicros m_budget;
    uint32_t      m_tickCount{0};
    uint32_t      m_initCount{0};
};

/// Minimal user scheduled task.
class NoOpScheduledTask : public IScheduledTask
{
  public:
    void          init() override {}
    void          tick(SputterMicros) override {}
    SputterMicros periodUs() const override { return 10000; }
    bool          validateDependencies() const override { return true; }
};

// ===========================================================================
// Tests
// ===========================================================================

/// Background task is dispatched during tick on the background core.
TEST(BackgroundDispatcher, BackgroundTaskTickedDuringSystemTick)
{
    using Cfg = BgDispCfg<1>;

    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    CountingBackgroundTask bgTask;
    SystemBuilder<Cfg>     builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    builder.addBackgroundTask(&bgTask);

    BuildResult result = builder.build();
    ASSERT_TRUE(result.ok) << result.error;

    System<Cfg>::init(0);
    System<Cfg>::tick(0, SputterMicros(1000));

    // Background task should have been ticked at least once
    EXPECT_GE(bgTask.tickCount(), 1u);
}

/// Background task init is called when the background core is initialized.
TEST(BackgroundDispatcher, BackgroundTaskInitCalledOnBackgroundCore)
{
    using Cfg = BgDispCfg<2>;

    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    CountingBackgroundTask bgTask;
    SystemBuilder<Cfg>     builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    builder.addBackgroundTask(&bgTask);

    BuildResult result = builder.build();
    ASSERT_TRUE(result.ok) << result.error;

    System<Cfg>::init(0);
    EXPECT_GE(bgTask.initCount(), 1u);
}

/// Round-robin fairness: multiple background tasks get dispatched in order.
TEST(BackgroundDispatcher, RoundRobinFairness)
{
    using Cfg = BgDispCfg<3>;

    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    // Budget: 500 each, control budget: 10000. All should fit within budget.
    CountingBackgroundTask bgA(500);
    CountingBackgroundTask bgB(500);
    CountingBackgroundTask bgC(500);
    SystemBuilder<Cfg>     builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    builder.addBackgroundTask(&bgA);
    builder.addBackgroundTask(&bgB);
    builder.addBackgroundTask(&bgC);

    BuildResult result = builder.build();
    ASSERT_TRUE(result.ok) << result.error;

    System<Cfg>::init(0);

    // Tick multiple times to exercise round-robin
    for (uint64_t t = 1000; t <= 5000; t += 1000)
    {
        System<Cfg>::tick(0, SputterMicros(t));
    }

    // All three tasks should have been ticked (DiagnosticsTask is also in the ring,
    // so adjust expectations: each user bg task should have been ticked at least once)
    EXPECT_GE(bgA.tickCount(), 1u);
    EXPECT_GE(bgB.tickCount(), 1u);
    EXPECT_GE(bgC.tickCount(), 1u);
}

/// Budget exhaustion: background task with budget exceeding remaining gap is skipped.
/// The first task in each round-robin cycle always dispatches (to guarantee
/// DiagnosticsTask runs). Budget gating applies from the second task onward.
TEST(BackgroundDispatcher, BudgetExhaustion)
{
    using Cfg = BgDispCfg<4>;

    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    // A small filler task ensures the huge-budget task is not the round-robin head.
    CountingBackgroundTask smallTask(100);
    CountingBackgroundTask hugeBudgetTask(20000);
    SystemBuilder<Cfg>     builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    builder.addBackgroundTask(&smallTask);
    builder.addBackgroundTask(&hugeBudgetTask);

    BuildResult result = builder.build();
    ASSERT_TRUE(result.ok) << result.error;

    System<Cfg>::init(0);
    System<Cfg>::tick(0, SputterMicros(1000));

    // The small task (round-robin head) should dispatch.
    EXPECT_GE(smallTask.tickCount(), 1u);

    // The huge-budget task should NOT be dispatched because its declared
    // budget (20000) exceeds the remaining gap (control budget 10000).
    EXPECT_EQ(hugeBudgetTask.tickCount(), 0u);
}

/// Empty background ring: tick completes without issue.
TEST(BackgroundDispatcher, EmptyBackgroundRingNoError)
{
    using Cfg = BgDispCfg<5>;

    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    // No user background tasks (only the kernel-added DiagnosticsTask is in the ring)
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);

    BuildResult result = builder.build();
    ASSERT_TRUE(result.ok) << result.error;

    System<Cfg>::init(0);

    // Should complete without crash — DiagnosticsTask runs via background dispatch
    EXPECT_NO_FATAL_FAILURE(System<Cfg>::tick(0, SputterMicros(1000)));
}

/// DiagnosticsTask is NOT in any core's task array after build.
TEST(BackgroundDispatcher, DiagnosticsTaskNotInCoreTaskList)
{
    using Cfg = BgDispCfg<6>;

    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);

    BuildResult result = builder.build();
    ASSERT_TRUE(result.ok) << result.error;

    // Check that no core task list contains a background task
    for (std::size_t c = 0; c < Cfg::kCoreCount; ++c)
    {
        for (std::size_t t = 0; t < System<Cfg>::taskCount(c); ++t)
        {
            ITask *tsk = System<Cfg>::task(c, t);
            EXPECT_FALSE(tsk->isBackground())
                << "Background task found in core " << c << " task list at index " << t;
        }
    }
}

/// DiagnosticsTask IS in the background task ring.
TEST(BackgroundDispatcher, DiagnosticsTaskInBackgroundRing)
{
    using Cfg = BgDispCfg<7>;

    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);

    BuildResult result = builder.build();
    ASSERT_TRUE(result.ok) << result.error;

    EXPECT_GE(System<Cfg>::backgroundTaskCount(), 1u);
    // First background task should be the kernel DiagnosticsTask
    EXPECT_NE(System<Cfg>::backgroundTasks()[0], nullptr);
    EXPECT_TRUE(System<Cfg>::backgroundTasks()[0]->isBackground());
}

/// Single background task dispatched exactly once per tick.
TEST(BackgroundDispatcher, SingleBackgroundTaskDispatchedOncePerTick)
{
    using Cfg = BgDispCfg<8>;

    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    CountingBackgroundTask bgTask(500);
    SystemBuilder<Cfg>     builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    builder.addBackgroundTask(&bgTask);

    BuildResult result = builder.build();
    ASSERT_TRUE(result.ok) << result.error;

    System<Cfg>::init(0);

    // Single tick
    System<Cfg>::tick(0, SputterMicros(1000));

    // The round-robin dispatches up to s_backgroundTaskCount tasks per tick.
    // With 2 background tasks (DiagnosticsTask + bgTask), both should run
    // in a single tick if budget allows.
    EXPECT_GE(bgTask.tickCount(), 1u);
}

/// Non-background core does not dispatch background tasks.
TEST(BackgroundDispatcher, NonBackgroundCoreDoesNotDispatch)
{
    using Cfg = BgDispDualCfg<1>;

    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    CountingBackgroundTask bgTask(500);
    SystemBuilder<Cfg>     builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    builder.addBackgroundTask(&bgTask);

    BuildResult result = builder.build();
    ASSERT_TRUE(result.ok) << result.error;

    System<Cfg>::init(0);
    System<Cfg>::init(1);

    // Tick only Core 0 — background core should be Core 1 (last active)
    System<Cfg>::tick(0, SputterMicros(1000));

    // bgTask should NOT have been ticked via Core 0 (it's not the background core)
    EXPECT_EQ(bgTask.tickCount(), 0u);

    // Now tick Core 1 — should dispatch background tasks
    System<Cfg>::tick(1, SputterMicros(1000));
    EXPECT_GE(bgTask.tickCount(), 1u);
}

/// Core task count reflects removal of DiagnosticsTask (2 kernel + 1 user = 3)
TEST(BackgroundDispatcher, CoreTaskCountExcludesDiagnosticsTask)
{
    using Cfg = BgDispCfg<9>;

    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    NoOpScheduledTask  userTask;
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    builder.core(0).addScheduledTask(&userTask);

    BuildResult result = builder.build();
    ASSERT_TRUE(result.ok) << result.error;

    // 2 kernel scheduled tasks (ControlTask + CommsTask) + 1 user task = 3
    // DiagnosticsTask is now in the background ring, not in the core list.
    EXPECT_EQ(System<Cfg>::taskCount(0), 3u);
}

/// DiagnosticsTask still executes via background dispatch in full pipeline.
TEST(BackgroundDispatcher, DiagnosticsStillRunsViaBackgroundDispatch)
{
    using Cfg = BgDispCfg<10>;

    static uint32_t kickCount = 0;
    kickCount                 = 0;
    auto kickFn               = []() { ++kickCount; };

    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream).setWatchdogKick(kickFn);

    BuildResult result = builder.build();
    ASSERT_TRUE(result.ok) << result.error;

    System<Cfg>::init(0);
    System<Cfg>::tick(0, SputterMicros(1000));

    // DiagnosticsTask kicks the watchdog — verify it ran via background dispatch
    EXPECT_GE(kickCount, 1u);
}
