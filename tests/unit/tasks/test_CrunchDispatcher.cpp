/**
 * @file test_CrunchDispatcher.cpp
 * @brief Unit tests for Kernel::CrunchDispatcher<Cfg>.
 *
 * Validates the tight-loop dispatcher: normal exit on stop condition,
 * overrun detection and ErrorLogger integration, consecutive overrun
 * abort threshold, safety abort forwarding, and watchdog kick.
 *
 * Each test uses a unique `DispCfg<N>` to isolate System<Cfg>
 * inline static state.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/13/2026
 */

#include "KernelTestAccess.h"
#include "MockSafetyMonitor.h"
#include "MockStreamReader.h"
#include "MockUserApplication.h"
#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/CrunchDispatcher.h"
#include "sputteros/kernel/System.h"
#include "sputteros/osal/tasks/ICrunchTask.h"
#include "sputteros/utils/logging/ErrorLogger.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <gtest/gtest.h>

using namespace SputterOS;
using namespace SputterOS::Kernel;
using ::testing::NiceMock;
using ::testing::Return;

// ===========================================================================
// Per-test isolated configuration (dual-core)
// ===========================================================================

template <int N> struct DispCfg
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
};

/// @brief Config with a low overrun threshold for testing escalation.
template <int N> struct DispCfgLowOverrun
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
    static constexpr uint32_t    kCrunchMaxOverruns  = 3;
};

// ===========================================================================
// Test doubles
// ===========================================================================

/**
 * @brief Configurable crunch task for dispatcher tests.
 *
 * Counts iterations, supports injected iteration time for overrun
 * simulation, and records abort calls.
 */
class TestCrunchTask : public ICrunchTask
{
  public:
    void init() override { m_initCalled = true; }
    void tick(SputterMicros) override {}

    void crunch(SputterMicros) override { ++m_crunchCount; }

    SputterMicros crunchPeriodUs() const override { return m_period; }
    SputterMicros maxIterationUs() const override { return m_maxIteration; }

    void onCrunchAbort() override { m_aborted = true; }

    uint32_t crunchCount() const { return m_crunchCount; }
    bool     initCalled() const { return m_initCalled; }
    bool     aborted() const { return m_aborted; }

    void setPeriod(SputterMicros p) { m_period = p; }
    void setMaxIteration(SputterMicros m) { m_maxIteration = m; }

  private:
    uint32_t      m_crunchCount{0};
    bool          m_initCalled{false};
    bool          m_aborted{false};
    SputterMicros m_period{163};
    SputterMicros m_maxIteration{200};
};

// ===========================================================================
// Normal loop exit on stop condition
// ===========================================================================

TEST(CrunchDispatcherTest, NormalExit_StopConditionFires)
{
    using Cfg = DispCfg<1>;
    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    TestCrunchTask     crunchTask;
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    builder.core(1).setCrunchTask(&crunchTask);

    static std::atomic<uint32_t> s_ticks{0};
    s_ticks = 0;
    builder.addStopCondition(
        []() -> bool
        {
            auto c = s_ticks.fetch_add(1, std::memory_order_relaxed);
            return c >= 5;
        });

    BuildResult result = builder.build();
    ASSERT_TRUE(result.ok) << result.error;

    System<Cfg>::run(1);

    EXPECT_GT(crunchTask.crunchCount(), 0u);
    EXPECT_FALSE(crunchTask.aborted());
}

// ===========================================================================
// Safety abort forwarding — onCrunchAbort() called
// ===========================================================================

TEST(CrunchDispatcherTest, SafetyAbort_CallsOnCrunchAbort)
{
    using Cfg = DispCfg<2>;
    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    TestCrunchTask     crunchTask;
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    builder.core(1).setCrunchTask(&crunchTask);

    // Add fallback stop condition
    builder.addStopCondition([]() -> bool { return true; });

    BuildResult result = builder.build();
    ASSERT_TRUE(result.ok) << result.error;

    // Signal safety abort before run
    System<Cfg>::signalSafetyAbort();
    System<Cfg>::run(1);

    EXPECT_TRUE(crunchTask.aborted());
}

TEST(CrunchDispatcherTest, SafetyAbort_LogsSoftAbort)
{
    using Cfg = DispCfg<3>;
    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    TestCrunchTask     crunchTask;
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    builder.core(1).setCrunchTask(&crunchTask);

    builder.addStopCondition([]() -> bool { return true; });

    BuildResult result = builder.build();
    ASSERT_TRUE(result.ok) << result.error;

    System<Cfg>::signalSafetyAbort();
    System<Cfg>::run(1);

    // Check ErrorLogger for SOFT_ABORT entry
    ErrorLogger::Entry entry{};
    bool               found = false;
    while (System<Cfg>::errorLogger().read(entry))
    {
        if (entry.code == ErrorLogger::ErrorCode::SOFT_ABORT)
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected SOFT_ABORT in ErrorLogger after safety abort";
}

// ===========================================================================
// Watchdog kick called every iteration
// ===========================================================================

static std::atomic<uint32_t> s_wdgKickCount{0};
static void                  testWatchdogKick() { s_wdgKickCount.fetch_add(1, std::memory_order_relaxed); }

TEST(CrunchDispatcherTest, WatchdogKick_CalledEveryIteration)
{
    using Cfg = DispCfg<4>;
    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    TestCrunchTask     crunchTask;
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    builder.setWatchdogKick(testWatchdogKick);
    builder.core(1).setCrunchTask(&crunchTask);

    s_wdgKickCount.store(0, std::memory_order_relaxed);

    static std::atomic<uint32_t> s_ticks2{0};
    s_ticks2 = 0;
    builder.addStopCondition(
        []() -> bool
        {
            auto c = s_ticks2.fetch_add(1, std::memory_order_relaxed);
            return c >= 3;
        });

    BuildResult result = builder.build();
    ASSERT_TRUE(result.ok) << result.error;

    System<Cfg>::run(1);

    // Watchdog kick should have been called at least as many times as crunch iterations
    EXPECT_GT(s_wdgKickCount.load(), 0u);
    EXPECT_GE(s_wdgKickCount.load(), crunchTask.crunchCount());
}

// ===========================================================================
// Overrun detection — CRUNCH_OVERRUN logged
// ===========================================================================

/**
 * @brief Crunch task that simulates overruns by advancing the system timer.
 *
 * On each crunch() call, bumps the timer source so that elapsed > maxIterationUs.
 */
static std::atomic<SputterMicros> s_fakeTime{0};

static SputterMicros fakeTimeFn() { return s_fakeTime.load(std::memory_order_relaxed); }

class OverrunCrunchTask : public ICrunchTask
{
  public:
    void init() override {}
    void tick(SputterMicros) override {}

    void crunch(SputterMicros) override
    {
        ++m_crunchCount;
        // Advance time by 300 µs to simulate overrun (max is 200 µs)
        s_fakeTime.fetch_add(300, std::memory_order_relaxed);
    }

    SputterMicros crunchPeriodUs() const override { return 163; }
    SputterMicros maxIterationUs() const override { return 200; }
    void          onCrunchAbort() override { m_aborted = true; }

    uint32_t crunchCount() const { return m_crunchCount; }
    bool     aborted() const { return m_aborted; }

  private:
    uint32_t m_crunchCount{0};
    bool     m_aborted{false};
};

TEST(CrunchDispatcherTest, OverrunDetection_LogsCrunchOverrun)
{
    using Cfg = DispCfgLowOverrun<1>;
    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    OverrunCrunchTask  crunchTask;
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    builder.core(1).setCrunchTask(&crunchTask);

    s_fakeTime.store(1000, std::memory_order_relaxed);
    builder.setClockSource(fakeTimeFn);

    BuildResult result = builder.build();
    ASSERT_TRUE(result.ok) << result.error;

    System<Cfg>::run(1);

    // With kCrunchMaxOverruns=3, the task should be aborted after 3 consecutive overruns
    EXPECT_TRUE(crunchTask.aborted());

    // Check for CRUNCH_OVERRUN entries in the error log
    ErrorLogger::Entry entry{};
    bool               foundOverrun = false;
    while (System<Cfg>::errorLogger().read(entry))
    {
        if (entry.code == ErrorLogger::ErrorCode::CRUNCH_OVERRUN)
        {
            foundOverrun = true;
            break;
        }
    }
    EXPECT_TRUE(foundOverrun) << "Expected CRUNCH_OVERRUN in ErrorLogger";
}

// ===========================================================================
// Consecutive overrun abort threshold
// ===========================================================================

TEST(CrunchDispatcherTest, ConsecutiveOverrunThreshold_AbortsTask)
{
    using Cfg = DispCfgLowOverrun<2>;
    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    OverrunCrunchTask  crunchTask;
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    builder.core(1).setCrunchTask(&crunchTask);

    s_fakeTime.store(1000, std::memory_order_relaxed);
    builder.setClockSource(fakeTimeFn);

    BuildResult result = builder.build();
    ASSERT_TRUE(result.ok) << result.error;

    System<Cfg>::run(1);

    EXPECT_TRUE(crunchTask.aborted());
    // crunchCount should be exactly kCrunchMaxOverruns (3) since every call overruns
    EXPECT_EQ(crunchTask.crunchCount(), 3u);
}

// ===========================================================================
// Consecutive overrun counter resets on clean iteration
// ===========================================================================

static std::atomic<uint32_t> s_mixedCallCount{0};

class MixedOverrunCrunchTask : public ICrunchTask
{
  public:
    void init() override {}
    void tick(SputterMicros) override {}

    void crunch(SputterMicros) override
    {
        ++m_crunchCount;
        uint32_t call = s_mixedCallCount.fetch_add(1, std::memory_order_relaxed);
        // Clean every 3rd call (positions 2, 5, 8, ...) resets consecutive counter.
        // Overrun on all other calls. With kCrunchMaxOverruns=3 the
        // consecutive count goes 1,2,0,1,2,0,… — never reaching threshold.
        if (call % 3 == 2)
        {
            // Clean iteration — advance by only 50 µs
            s_fakeTime.fetch_add(50, std::memory_order_relaxed);
        }
        else
        {
            // Overrun — advance by 300 µs
            s_fakeTime.fetch_add(300, std::memory_order_relaxed);
        }
    }

    SputterMicros crunchPeriodUs() const override { return 163; }
    SputterMicros maxIterationUs() const override { return 200; }
    void          onCrunchAbort() override { m_aborted = true; }

    uint32_t crunchCount() const { return m_crunchCount; }
    bool     aborted() const { return m_aborted; }

  private:
    uint32_t m_crunchCount{0};
    bool     m_aborted{false};
};

TEST(CrunchDispatcherTest, OverrunCounterResets_OnCleanIteration)
{
    using Cfg = DispCfgLowOverrun<3>;
    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    MixedOverrunCrunchTask crunchTask;
    SystemBuilder<Cfg>     builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    builder.core(1).setCrunchTask(&crunchTask);

    s_fakeTime.store(1000, std::memory_order_relaxed);
    s_mixedCallCount.store(0, std::memory_order_relaxed);
    builder.setClockSource(fakeTimeFn);

    // Need a stop condition as fallback (task shouldn't abort because
    // the consecutive count resets at call 2)
    static std::atomic<uint32_t> s_stopCount{0};
    s_stopCount = 0;
    builder.addStopCondition(
        []() -> bool
        {
            auto c = s_stopCount.fetch_add(1, std::memory_order_relaxed);
            return c >= 10;
        });

    BuildResult result = builder.build();
    ASSERT_TRUE(result.ok) << result.error;

    System<Cfg>::run(1);

    // The task should NOT have been aborted — consecutive never reached 3
    // because call 2 was clean, resetting the counter.
    // After call 2 (clean), calls 3 and 4 overrun (consecutive = 2),
    // then the stop condition fires before reaching 3.
    EXPECT_FALSE(crunchTask.aborted());
    EXPECT_GT(crunchTask.crunchCount(), 3u);
}

// ===========================================================================
// Dispatcher accessors — totalOverruns and consecutiveOverruns
// ===========================================================================

TEST(CrunchDispatcherTest, Accessors_TrackOverrunCounts)
{
    CrunchDispatcher<DispCfg<5>> dispatcher;

    // Before configure — defaults
    EXPECT_EQ(dispatcher.totalOverruns(), 0u);
    EXPECT_EQ(dispatcher.consecutiveOverruns(), 0u);
    EXPECT_EQ(dispatcher.task(), nullptr);

    TestCrunchTask crunchTask;
    dispatcher.configure(&crunchTask);

    EXPECT_EQ(dispatcher.task(), &crunchTask);
}
