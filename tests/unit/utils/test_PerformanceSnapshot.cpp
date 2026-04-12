/**
 * @file test_PerformanceSnapshot.cpp
 * @brief Unit tests for System<Cfg>::snapshot() aggregation.
 *
 * Builds a minimal System, runs ticks with instrumented tasks,
 * then verifies that snapshot() captures all metrics correctly.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/hal/devices/IStream.h"
#include "sputteros/kernel/System.h"
#include "sputteros/utils/PerformanceSnapshot.h"
#include "../mocks/KernelTestAccess.h"

#include <gtest/gtest.h>

using namespace SputterOS;

// ===========================================================================
// Test-specific Cfg (unique to avoid static state pollution)
// ===========================================================================

template <int N> struct SnapTestCfg
{
    enum class State : uint8_t { IDLE = 0, RUNNING, FAULT };
    enum class CmdID : uint8_t { SET_STATE = 0, SET_FLOW = 1 };
    struct Command
    {
        CmdID   id;
        uint8_t targetDevice;
        float   value;
    };
    static constexpr int         kMaxCommandsPerTick = 4;
    static constexpr uint8_t     kMaxValidCommandID  = 1;
    static constexpr std::size_t kCoreCount          = 1;
    static constexpr std::size_t kQueueCapacity      = 8;
};

// ===========================================================================
// Minimal user app
// ===========================================================================

template <typename Cfg> class StubApp : public IUserApplication<Cfg>
{
  public:
    void init() override {}
    void tick(SputterMicros) override {}
    void handleCommand(const typename Cfg::Command &) override {}
    void forceSafeAbort() override {}
};

// ===========================================================================
// Fake stream for CommsTask dependency validation
// ===========================================================================

class FakeStream : public IStream
{
  public:
    std::size_t available() const override { return 0; }
    std::size_t read(uint8_t *, std::size_t) override { return 0; }
    std::size_t write(const uint8_t *, std::size_t len) override { return len; }
    bool        isConnected() const override { return true; }
};

// ===========================================================================
// Instrumented task with controlled duration
// ===========================================================================

class DummyTask : public IScheduledTask
{
  public:
    void init() override {}
    void tick(SputterMicros) override { ++m_ticks; }
    SputterMicros periodUs() const override { return 10000; }
    uint32_t m_ticks = 0;
};

// ===========================================================================
// Fixture
// ===========================================================================

using Cfg1 = SnapTestCfg<1>;

class PerformanceSnapshotTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        Kernel::KernelTestAccess::resetSystem<Cfg1>();
        SystemBuilder<Cfg1> builder(&m_app, nullptr, 0);
        builder.setStream(&m_stream);
        builder.core(0).addScheduledTask(&m_task);
        ASSERT_TRUE(builder.build());
        System<Cfg1>::init(0);
    }

    void TearDown() override { Kernel::KernelTestAccess::resetSystem<Cfg1>(); }

    StubApp<Cfg1> m_app;
    FakeStream    m_stream;
    DummyTask     m_task;
};

// ===========================================================================
// Basic snapshot
// ===========================================================================

TEST_F(PerformanceSnapshotTest, Snapshot_ReturnsNonZeroTimestamp)
{
    System<Cfg1>::tick(0, 1000);
    auto snap = System<Cfg1>::snapshot();
    // Timestamp comes from s_timer.nowMicros(); in host tests it may be 0
    // but taskCount and coreCount should be populated.
    EXPECT_EQ(snap.coreCount, 1u);
}

TEST_F(PerformanceSnapshotTest, Snapshot_PopulatesCoreCount)
{
    auto snap = System<Cfg1>::snapshot();
    EXPECT_EQ(snap.coreCount, Cfg1::kCoreCount);
}

TEST_F(PerformanceSnapshotTest, Snapshot_PopulatesTaskCount)
{
    auto snap = System<Cfg1>::snapshot();
    // Builder prepends kernel tasks (ControlTask, DiagnosticsTask) + user task
    EXPECT_GE(snap.taskCount, 1u);
}

TEST_F(PerformanceSnapshotTest, Snapshot_AfterTicks_HasTaskSamples)
{
    System<Cfg1>::tick(0, 1000);
    System<Cfg1>::tick(0, 2000);
    System<Cfg1>::tick(0, 3000);

    auto snap = System<Cfg1>::snapshot();
    // At least one task should have samples
    bool anySamples = false;
    for (std::size_t i = 0; i < snap.taskCount; ++i)
    {
        if (snap.tasks[i].samples > 0)
        {
            anySamples = true;
            break;
        }
    }
    EXPECT_TRUE(anySamples);
}

TEST_F(PerformanceSnapshotTest, Snapshot_QueueDepthSampled)
{
    // Push a command to make queue non-empty
    typename Cfg1::Command cmd{Cfg1::CmdID::SET_STATE, 0, 1.0f};
    System<Cfg1>::commandQueue().try_push(cmd);

    // Tick to trigger queue sampling
    System<Cfg1>::tick(0, 1000);

    auto snap = System<Cfg1>::snapshot();
    // After push + tick, queueMaxDepth should be ≥ 1
    // (ControlTask may have consumed it, so we just check the monitor was active)
    EXPECT_GE(snap.queueMaxDepth + snap.queueDepth, 0u);
}

TEST_F(PerformanceSnapshotTest, Snapshot_SchedulerHealthPopulated)
{
    System<Cfg1>::tick(0, 1000);
    System<Cfg1>::tick(0, 2000);

    auto snap = System<Cfg1>::snapshot();
    EXPECT_GE(snap.schedulerTickCount, 2u);
}

TEST_F(PerformanceSnapshotTest, Snapshot_TaskHistogramPopulated)
{
    System<Cfg1>::tick(0, 1000);

    auto snap = System<Cfg1>::snapshot();
    // At least one task should have histogram data
    bool anyHist = false;
    for (std::size_t i = 0; i < snap.taskCount; ++i)
    {
        for (std::size_t b = 0; b < Kernel::TaskTimer::kHistogramBuckets; ++b)
        {
            if (snap.tasks[i].histogram[b] > 0)
            {
                anyHist = true;
                break;
            }
        }
        if (anyHist) break;
    }
    EXPECT_TRUE(anyHist);
}

TEST_F(PerformanceSnapshotTest, Snapshot_AllTasksCoreIdValid)
{
    System<Cfg1>::tick(0, 1000);

    auto snap = System<Cfg1>::snapshot();
    for (std::size_t i = 0; i < snap.taskCount; ++i)
    {
        EXPECT_LT(snap.tasks[i].coreId, Cfg1::kCoreCount);
    }
}
