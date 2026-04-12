/**
 * @file test_PerformanceMonitoring.cpp
 * @brief System integration test for the performance monitoring pipeline.
 *
 * Builds a full System with real kernel tasks, runs multiple ticks,
 * then captures a PerformanceSnapshot and formats it as both
 * key-value and CSV to validate the end-to-end monitoring pipeline.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "SystemTestConfig.h"
#include "SystemTestHelpers.h"

#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/System.h"
#include "sputteros/utils/PerformanceFormatter.h"
#include "sputteros/utils/PerformanceSnapshot.h"

#include "../../unit/mocks/KernelTestAccess.h"

#include <gtest/gtest.h>
#include <string>

using namespace SputterOS;
using namespace SputterOS::SystemTests;

// ===========================================================================
// Config — unique type to isolate static state
// ===========================================================================

struct PerfMonCfg
{
    enum class State : uint8_t { IDLE = 0, RUNNING, FAULT };
    enum class CmdID : uint8_t { SET_STATE = 0, SET_FLOW = 1, ABORT = 2 };
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

// ===========================================================================
// Fixture
// ===========================================================================

class PerformanceMonitoringTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        Kernel::KernelTestAccess::resetSystem<PerfMonCfg>();

        SystemBuilder<PerfMonCfg> builder(&m_app, monitors, 1);
        builder.setStream(&m_stream).setWatchdogKick(nullptr);
        builder.core(0).addScheduledTask(&m_userTask);
        ASSERT_TRUE(builder.build());
        System<PerfMonCfg>::init(0);
    }

    void TearDown() override { Kernel::KernelTestAccess::resetSystem<PerfMonCfg>(); }

    InstrumentedApp<PerfMonCfg> m_app;
    FakeStreamReader            m_stream;
    AlwaysSafeSafetyMonitor     m_monitor;
    ISafetyMonitor             *monitors[1] = {&m_monitor};
    InstrumentedTask            m_userTask;
};

// ===========================================================================
// Snapshot captures data after multiple ticks
// ===========================================================================

TEST_F(PerformanceMonitoringTest, Snapshot_AfterTicks_PopulatesAllFields)
{
    constexpr int kTicks = 20;
    SputterMicros t      = 10000;
    for (int i = 0; i < kTicks; ++i)
    {
        System<PerfMonCfg>::tick(0, t);
        t += 10000; // 10 ms per tick
    }

    auto snap = System<PerfMonCfg>::snapshot();

    EXPECT_EQ(snap.coreCount, PerfMonCfg::kCoreCount);
    EXPECT_GE(snap.taskCount, 1u);

    // Should have scheduler tick data
    EXPECT_GE(snap.schedulerTickCount, static_cast<uint32_t>(kTicks));

    // At least one task with samples
    bool foundSamples = false;
    for (std::size_t i = 0; i < snap.taskCount; ++i)
    {
        if (snap.tasks[i].samples > 0)
        {
            foundSamples = true;
            // Verify task metadata
            EXPECT_LT(snap.tasks[i].coreId, PerfMonCfg::kCoreCount);
            // Histogram should have at least one count
            uint32_t histTotal = 0;
            for (std::size_t b = 0; b < Kernel::TaskTimer::kHistogramBuckets; ++b)
            {
                histTotal += snap.tasks[i].histogram[b];
            }
            EXPECT_EQ(histTotal, snap.tasks[i].samples)
                << "Histogram total should equal sample count for task " << i;
        }
    }
    EXPECT_TRUE(foundSamples);
}

// ===========================================================================
// Queue depth is sampled during ticks
// ===========================================================================

TEST_F(PerformanceMonitoringTest, Snapshot_QueueDepthReflectsActivity)
{
    // Tick once to initialize the queue monitor
    System<PerfMonCfg>::tick(0, 10000);

    // Push commands after the tick (so ControlTask can't drain them yet)
    for (int i = 0; i < 5; ++i)
    {
        typename PerfMonCfg::Command cmd{PerfMonCfg::CmdID::SET_STATE, 0, static_cast<float>(i)};
        System<PerfMonCfg>::commandQueue().try_push(cmd);
    }

    // Next tick: queue is sampled first by System::tick, then ControlTask drains
    // But actually the sampling happens after tasks run. So push more after tick
    // to verify the monitor tracks *something*.
    System<PerfMonCfg>::tick(0, 20000);

    // Push again — queue now has items post-drain
    for (int i = 0; i < 3; ++i)
    {
        typename PerfMonCfg::Command cmd{PerfMonCfg::CmdID::SET_FLOW, 1, static_cast<float>(i)};
        System<PerfMonCfg>::commandQueue().try_push(cmd);
    }

    // This tick samples queue with 3 items before ControlTask drains
    System<PerfMonCfg>::tick(0, 30000);

    auto snap = System<PerfMonCfg>::snapshot();
    // The monitor should have recorded samples across multiple ticks
    EXPECT_GE(System<PerfMonCfg>::queueMonitor().sampleCount(), 3u);
}

// ===========================================================================
// Formatter produces parseable key-value output
// ===========================================================================

TEST_F(PerformanceMonitoringTest, FormatKeyValue_ProducesParseableOutput)
{
    System<PerfMonCfg>::tick(0, 10000);
    System<PerfMonCfg>::tick(0, 20000);

    auto snap = System<PerfMonCfg>::snapshot();

    char buf[4096]{};
    auto len = PerformanceFormatter::formatKeyValue(snap, buf, sizeof(buf));

    ASSERT_GT(len, 0u);
    std::string out(buf, len);

    // Spot-check key fields exist
    EXPECT_NE(out.find("timestamp="), std::string::npos);
    EXPECT_NE(out.find("cores=1\n"), std::string::npos);
    EXPECT_NE(out.find("task_count="), std::string::npos);
    EXPECT_NE(out.find("sched_ticks="), std::string::npos);
    EXPECT_NE(out.find("t0_"), std::string::npos) << "Should have at least one task entry";
}

// ===========================================================================
// Formatter produces parseable CSV output
// ===========================================================================

TEST_F(PerformanceMonitoringTest, FormatCSV_ProducesParseableOutput)
{
    System<PerfMonCfg>::tick(0, 10000);

    auto snap = System<PerfMonCfg>::snapshot();

    char buf[4096]{};
    auto len = PerformanceFormatter::formatCSV(snap, buf, sizeof(buf));

    ASSERT_GT(len, 0u);
    std::string out(buf, len);

    // Should have system header and task header
    EXPECT_NE(out.find("timestamp,cores,"), std::string::npos);
    EXPECT_NE(out.find("task_idx,core,"), std::string::npos);

    // Count newlines to verify structure: header + data + task_header + task_rows
    int newlines = 0;
    for (char c : out)
    {
        if (c == '\n')
            ++newlines;
    }
    // At minimum: system header, system data, task header, ≥1 task row
    EXPECT_GE(newlines, 4);
}

// ===========================================================================
// Snapshot is a value copy — mutation after capture has no effect
// ===========================================================================

TEST_F(PerformanceMonitoringTest, Snapshot_IsValueCopy)
{
    System<PerfMonCfg>::tick(0, 10000);
    auto snap1 = System<PerfMonCfg>::snapshot();

    // Run more ticks to change state
    System<PerfMonCfg>::tick(0, 20000);
    System<PerfMonCfg>::tick(0, 30000);
    auto snap2 = System<PerfMonCfg>::snapshot();

    // snap1 should not have been mutated by later ticks
    EXPECT_NE(snap1.schedulerTickCount, snap2.schedulerTickCount);
}
