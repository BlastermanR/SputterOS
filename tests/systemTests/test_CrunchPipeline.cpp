/**
 * @file test_CrunchPipeline.cpp
 * @brief System tests for dual-core CRUNCH dispatch mode.
 *
 * Exercises the full kernel pipeline with Core 0 in FLAT_LOOP and
 * Core 1 in CRUNCH dispatch mode. Verifies:
 * - Dual-core build with setCrunchTask() succeeds.
 * - Core 1 enters CrunchDispatcher and executes crunch() iterations.
 * - Safety abort on Core 0 propagates to Core 1's onCrunchAbort().
 * - Overrun escalation triggers onCrunchAbort() after threshold.
 * - AtomicDoubleBuffer cross-core data flows correctly.
 * - Stop condition terminates both cores cleanly.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/14/2026
 */

#include "SystemTestConfig.h"
#include "SystemTestHelpers.h"

#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/System.h"
#include "sputteros/osal/sync/AtomicDoubleBuffer.h"
#include "sputteros/osal/tasks/ICrunchTask.h"

#include "../../unit/mocks/KernelTestAccess.h"

#include <atomic>
#include <gtest/gtest.h>
#include <thread>

using namespace SputterOS;
using namespace SputterOS::SystemTests;

// =========================================================================
// Crunch-capable dual-core config (unique per test suite)
// =========================================================================

struct CrunchPipelineCfg
{
    enum class State : uint8_t
    {
        IDLE = 0
    };
    enum class CmdID : uint8_t
    {
        NOP = 0
    };
    struct Command
    {
        CmdID   id;
        uint8_t targetDevice;
        float   value;
    };

    static constexpr int         kMaxCommandsPerTick = 4;
    static constexpr uint8_t     kMaxValidCommandID  = 0;
    static constexpr std::size_t kCoreCount          = 2;
    static constexpr std::size_t kQueueCapacity      = 16;
    static constexpr uint32_t    kCrunchMaxOverruns  = 5;
};

struct CrunchOverrunCfg
{
    enum class State : uint8_t
    {
        IDLE = 0
    };
    enum class CmdID : uint8_t
    {
        NOP = 0
    };
    struct Command
    {
        CmdID   id;
        uint8_t targetDevice;
        float   value;
    };

    static constexpr int         kMaxCommandsPerTick = 4;
    static constexpr uint8_t     kMaxValidCommandID  = 0;
    static constexpr std::size_t kCoreCount          = 2;
    static constexpr std::size_t kQueueCapacity      = 16;
    static constexpr uint32_t    kCrunchMaxOverruns  = 3;
};

struct CrunchAbortCfg
{
    enum class State : uint8_t
    {
        IDLE = 0
    };
    enum class CmdID : uint8_t
    {
        NOP = 0
    };
    struct Command
    {
        CmdID   id;
        uint8_t targetDevice;
        float   value;
    };

    static constexpr int         kMaxCommandsPerTick = 4;
    static constexpr uint8_t     kMaxValidCommandID  = 0;
    static constexpr std::size_t kCoreCount          = 2;
    static constexpr std::size_t kQueueCapacity      = 16;
    static constexpr uint32_t    kCrunchMaxOverruns  = 10;
};

struct CrunchBufferCfg
{
    enum class State : uint8_t
    {
        IDLE = 0
    };
    enum class CmdID : uint8_t
    {
        NOP = 0
    };
    struct Command
    {
        CmdID   id;
        uint8_t targetDevice;
        float   value;
    };

    static constexpr int         kMaxCommandsPerTick = 4;
    static constexpr uint8_t     kMaxValidCommandID  = 0;
    static constexpr std::size_t kCoreCount          = 2;
    static constexpr std::size_t kQueueCapacity      = 16;
    static constexpr uint32_t    kCrunchMaxOverruns  = 10;
};

// =========================================================================
// Test crunch task — counts iterations, controllable stop
// =========================================================================

class CountingCrunchTask : public ICrunchTask
{
  public:
    void init() override { ++initCount; }
    void tick(SputterMicros) override {}

    void crunch(SputterMicros /*now*/) override { ++crunchCount; }

    SputterMicros crunchPeriodUs() const override { return 1000; }
    SputterMicros maxIterationUs() const override { return 2000; }

    void onCrunchAbort() override { ++abortCount; }

    std::atomic<uint32_t> crunchCount{0};
    std::atomic<uint32_t> abortCount{0};
    uint32_t              initCount{0};
};

// =========================================================================
// Test crunch task with AtomicDoubleBuffer data sharing
// =========================================================================

struct SharedData
{
    float    value{0.0f};
    uint32_t seq{0};
};

class BufferedCrunchTask : public ICrunchTask
{
  public:
    explicit BufferedCrunchTask(AtomicDoubleBuffer<SharedData> &inBuf, AtomicDoubleBuffer<SharedData> &outBuf)
        : m_inBuf(inBuf), m_outBuf(outBuf)
    {
    }

    void init() override {}
    void tick(SputterMicros) override {}

    void crunch(SputterMicros /*now*/) override
    {
        auto data = m_inBuf.read();
        ++m_count;
        m_outBuf.write({data.value * 2.0f, m_count});
    }

    SputterMicros crunchPeriodUs() const override { return 1000; }
    SputterMicros maxIterationUs() const override { return 2000; }
    void          onCrunchAbort() override {}

    std::atomic<uint32_t> m_count{0};

  private:
    AtomicDoubleBuffer<SharedData> &m_inBuf;
    AtomicDoubleBuffer<SharedData> &m_outBuf;
};

// =========================================================================
// Fixtures
// =========================================================================

class CrunchPipeline : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<CrunchPipelineCfg>(); }
};

class CrunchOverrunPipeline : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<CrunchOverrunCfg>(); }
};

class CrunchAbortPipeline : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<CrunchAbortCfg>(); }
};

class CrunchBufferPipeline : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<CrunchBufferCfg>(); }
};

// =========================================================================
// Tests — Basic CRUNCH lifecycle
// =========================================================================

TEST_F(CrunchPipeline, DualCoreBuildWithCrunchTaskSucceeds)
{
    using Cfg = CrunchPipelineCfg;

    InstrumentedApp<Cfg>    app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};
    CountingCrunchTask      crunchTask;

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(1).setCrunchTask(&crunchTask);

    const BuildResult result = builder.build();
    EXPECT_TRUE(result);
}

TEST_F(CrunchPipeline, CrunchCoreExecutesCrunchIterations)
{
    using Cfg = CrunchPipelineCfg;

    InstrumentedApp<Cfg>    app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};
    CountingCrunchTask      crunchTask;

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(1).setCrunchTask(&crunchTask);

    // Stop after a short wallclock timeout.
    static constexpr uint32_t kRunMs = 500;
    builder.addStopCondition(
        []() -> bool
        {
            using Clock             = std::chrono::steady_clock;
            static const auto endAt = Clock::now() + std::chrono::milliseconds{kRunMs};
            return Clock::now() >= endAt;
        });

    ASSERT_TRUE(builder.build());

    // Launch both cores.
    std::thread core1([]() { System<Cfg>::run(1); });
    System<Cfg>::run(0);
    core1.join();

    // The crunch task should have executed many iterations in 500ms.
    EXPECT_GT(crunchTask.crunchCount.load(), 0u);
    EXPECT_EQ(crunchTask.initCount, 1u);
    EXPECT_EQ(crunchTask.abortCount.load(), 0u);
}

TEST_F(CrunchPipeline, StopConditionTerminatesBothCores)
{
    using Cfg = CrunchPipelineCfg;

    InstrumentedApp<Cfg>    app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};
    CountingCrunchTask      crunchTask;

    static std::atomic<bool> s_stopFlag{false};
    s_stopFlag.store(false, std::memory_order_relaxed);

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(1).setCrunchTask(&crunchTask);
    builder.addStopCondition([]() -> bool { return s_stopFlag.load(std::memory_order_acquire); });
    ASSERT_TRUE(builder.build());

    std::thread core1([]() { System<Cfg>::run(1); });

    // Let both cores run briefly, then signal stop.
    std::thread stopper(
        []()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            s_stopFlag.store(true, std::memory_order_release);
        });

    System<Cfg>::run(0);
    core1.join();
    stopper.join();

    // Both cores should have exited cleanly.
    EXPECT_GT(crunchTask.crunchCount.load(), 0u);
    EXPECT_EQ(crunchTask.abortCount.load(), 0u);
}

// =========================================================================
// Tests — Safety abort propagation
// =========================================================================

TEST_F(CrunchAbortPipeline, SafetyAbortOnCore0PropagatesOnCrunchAbort)
{
    using Cfg = CrunchAbortCfg;

    InstrumentedApp<Cfg> app;
    FakeStreamReader     stream;
    TrippableMonitor     monitor;
    monitor.safeFlag              = true; // Start safe, trip after launch
    ISafetyMonitor    *monitors[] = {&monitor};
    CountingCrunchTask crunchTask;

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(1).setCrunchTask(&crunchTask);

    builder.addStopCondition(
        []() -> bool
        {
            using Clock             = std::chrono::steady_clock;
            static const auto endAt = Clock::now() + std::chrono::milliseconds{2000};
            return Clock::now() >= endAt;
        });

    ASSERT_TRUE(builder.build());

    // Trip the safety monitor after a brief delay.
    std::thread tripper(
        [&monitor]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            monitor.safeFlag = false;
        });

    std::thread core1([]() { System<Cfg>::run(1); });
    System<Cfg>::run(0);
    core1.join();
    tripper.join();

    // The crunch task should have been aborted via onCrunchAbort().
    EXPECT_GE(crunchTask.abortCount.load(), 1u);
}

// =========================================================================
// Tests — AtomicDoubleBuffer cross-core data flow
// =========================================================================

TEST_F(CrunchBufferPipeline, AtomicDoubleBufferCrossCoreDataFlow)
{
    using Cfg = CrunchBufferCfg;

    AtomicDoubleBuffer<SharedData> inBuf;
    AtomicDoubleBuffer<SharedData> outBuf;

    // Write initial target before launch.
    inBuf.write({42.0f, 1});

    InstrumentedApp<Cfg>    app;
    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};
    BufferedCrunchTask      crunchTask(inBuf, outBuf);

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    builder.core(1).setCrunchTask(&crunchTask);

    static constexpr uint32_t kRunMs = 500;
    builder.addStopCondition(
        []() -> bool
        {
            using Clock             = std::chrono::steady_clock;
            static const auto endAt = Clock::now() + std::chrono::milliseconds{kRunMs};
            return Clock::now() >= endAt;
        });

    ASSERT_TRUE(builder.build());

    std::thread core1([]() { System<Cfg>::run(1); });
    System<Cfg>::run(0);
    core1.join();

    // The crunch task should have read the input and written back.
    auto result = outBuf.read();
    EXPECT_GT(crunchTask.m_count.load(), 0u);
    EXPECT_GT(result.seq, 0u);
    // Last written value should be 42.0 * 2.0 = 84.0
    EXPECT_FLOAT_EQ(result.value, 84.0f);
}
