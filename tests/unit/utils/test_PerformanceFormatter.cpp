/**
 * @file test_PerformanceFormatter.cpp
 * @brief Unit tests for PerformanceFormatter.
 *
 * Validates key-value and CSV output format with known snapshot data.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "sputteros/utils/PerformanceFormatter.h"
#include "sputteros/utils/PerformanceSnapshot.h"
#include <cstring>
#include <gtest/gtest.h>
#include <string>

using namespace SputterOS;

// ===========================================================================
// Helpers
// ===========================================================================

/**
 * @brief Build a deterministic snapshot for formatter testing.
 */
static PerformanceSnapshot makeTestSnapshot()
{
    PerformanceSnapshot snap{};
    snap.timestamp           = 1000000;
    snap.coreCount           = 2;
    snap.coreUtilization[0]  = 0.25f;
    snap.coreUtilization[1]  = 0.10f;
    snap.queueDepth          = 3;
    snap.queueMaxDepth       = 7;
    snap.queueAvgDepth       = 2.5f;
    snap.peakHeapUsed        = 1024;
    snap.freeHeap            = 4096;
    snap.stackHighWater      = 512;
    snap.totalGapUs          = 50000;
    snap.maxGapUs            = 200;
    snap.avgGapUs            = 50.0f;
    snap.schedulerTickCount  = 1000;
    snap.totalOverruns       = 2;
    snap.totalDeadlineMisses = 1;

    snap.taskCount          = 1;
    snap.tasks[0].taskIndex = 0;
    snap.tasks[0].coreId    = 0;
    snap.tasks[0].lastUs    = 100;
    snap.tasks[0].minUs     = 80;
    snap.tasks[0].maxUs     = 150;
    snap.tasks[0].avgUs     = 110.0f;
    snap.tasks[0].samples   = 500;
    snap.tasks[0].overruns  = 1;
    snap.tasks[0].misses    = 0;
    for (std::size_t b = 0; b < Kernel::TaskTimer::kHistogramBuckets; ++b)
    {
        snap.tasks[0].histogram[b] = 0;
    }
    snap.tasks[0].histogram[0] = 490;
    snap.tasks[0].histogram[1] = 10;

    return snap;
}

// ===========================================================================
// Fixture
// ===========================================================================

class PerformanceFormatterTest : public ::testing::Test
{
  protected:
    static constexpr std::size_t kBufSize = 4096;
    char                         m_buf[kBufSize]{};
};

// ===========================================================================
// Key-Value format
// ===========================================================================

TEST_F(PerformanceFormatterTest, KeyValue_ContainsTimestamp)
{
    auto snap = makeTestSnapshot();
    auto len  = PerformanceFormatter::formatKeyValue(snap, m_buf, kBufSize);

    ASSERT_GT(len, 0u);
    std::string out(m_buf, len);
    EXPECT_NE(out.find("timestamp=1000000\n"), std::string::npos);
}

TEST_F(PerformanceFormatterTest, KeyValue_ContainsCoreCount)
{
    auto snap = makeTestSnapshot();
    PerformanceFormatter::formatKeyValue(snap, m_buf, kBufSize);

    std::string out(m_buf);
    EXPECT_NE(out.find("cores=2\n"), std::string::npos);
}

TEST_F(PerformanceFormatterTest, KeyValue_ContainsCoreUtilization)
{
    auto snap = makeTestSnapshot();
    PerformanceFormatter::formatKeyValue(snap, m_buf, kBufSize);

    std::string out(m_buf);
    EXPECT_NE(out.find("core0_util="), std::string::npos);
    EXPECT_NE(out.find("core1_util="), std::string::npos);
}

TEST_F(PerformanceFormatterTest, KeyValue_ContainsQueueMetrics)
{
    auto snap = makeTestSnapshot();
    PerformanceFormatter::formatKeyValue(snap, m_buf, kBufSize);

    std::string out(m_buf);
    EXPECT_NE(out.find("queue_depth=3\n"), std::string::npos);
    EXPECT_NE(out.find("queue_max=7\n"), std::string::npos);
    EXPECT_NE(out.find("queue_avg="), std::string::npos);
}

TEST_F(PerformanceFormatterTest, KeyValue_ContainsMemoryMetrics)
{
    auto snap = makeTestSnapshot();
    PerformanceFormatter::formatKeyValue(snap, m_buf, kBufSize);

    std::string out(m_buf);
    EXPECT_NE(out.find("heap_peak=1024\n"), std::string::npos);
    EXPECT_NE(out.find("heap_free=4096\n"), std::string::npos);
    EXPECT_NE(out.find("stack_hw=512\n"), std::string::npos);
}

TEST_F(PerformanceFormatterTest, KeyValue_ContainsSchedulerHealth)
{
    auto snap = makeTestSnapshot();
    PerformanceFormatter::formatKeyValue(snap, m_buf, kBufSize);

    std::string out(m_buf);
    EXPECT_NE(out.find("gap_total=50000\n"), std::string::npos);
    EXPECT_NE(out.find("gap_max=200\n"), std::string::npos);
    EXPECT_NE(out.find("overruns=2\n"), std::string::npos);
    EXPECT_NE(out.find("deadline_misses=1\n"), std::string::npos);
    EXPECT_NE(out.find("sched_ticks=1000\n"), std::string::npos);
}

TEST_F(PerformanceFormatterTest, KeyValue_ContainsTaskData)
{
    auto snap = makeTestSnapshot();
    PerformanceFormatter::formatKeyValue(snap, m_buf, kBufSize);

    std::string out(m_buf);
    EXPECT_NE(out.find("task_count=1\n"), std::string::npos);
    EXPECT_NE(out.find("t0_core=0\n"), std::string::npos);
    EXPECT_NE(out.find("t0_last=100\n"), std::string::npos);
    EXPECT_NE(out.find("t0_min=80\n"), std::string::npos);
    EXPECT_NE(out.find("t0_max=150\n"), std::string::npos);
    EXPECT_NE(out.find("t0_samples=500\n"), std::string::npos);
    EXPECT_NE(out.find("t0_overruns=1\n"), std::string::npos);
    EXPECT_NE(out.find("t0_misses=0\n"), std::string::npos);
}

TEST_F(PerformanceFormatterTest, KeyValue_ContainsHistogram)
{
    auto snap = makeTestSnapshot();
    PerformanceFormatter::formatKeyValue(snap, m_buf, kBufSize);

    std::string out(m_buf);
    EXPECT_NE(out.find("t0_hist=490,10,0,0,0,0,0,0\n"), std::string::npos);
}

TEST_F(PerformanceFormatterTest, KeyValue_NullTerminated)
{
    auto snap = makeTestSnapshot();
    auto len  = PerformanceFormatter::formatKeyValue(snap, m_buf, kBufSize);

    EXPECT_EQ(m_buf[len], '\0');
}

// ===========================================================================
// CSV format
// ===========================================================================

TEST_F(PerformanceFormatterTest, CSV_ContainsSystemHeader)
{
    auto snap = makeTestSnapshot();
    auto len  = PerformanceFormatter::formatCSV(snap, m_buf, kBufSize);

    ASSERT_GT(len, 0u);
    std::string out(m_buf, len);
    EXPECT_NE(out.find("timestamp,cores,queue_depth"), std::string::npos);
}

TEST_F(PerformanceFormatterTest, CSV_ContainsTaskHeader)
{
    auto snap = makeTestSnapshot();
    PerformanceFormatter::formatCSV(snap, m_buf, kBufSize);

    std::string out(m_buf);
    EXPECT_NE(out.find("task_idx,core,last,min,max,avg,samples,overruns,misses"), std::string::npos);
}

TEST_F(PerformanceFormatterTest, CSV_ContainsSystemDataRow)
{
    auto snap = makeTestSnapshot();
    PerformanceFormatter::formatCSV(snap, m_buf, kBufSize);

    std::string out(m_buf);
    // System data row starts with timestamp
    EXPECT_NE(out.find("1000000,2,3,7,"), std::string::npos);
}

TEST_F(PerformanceFormatterTest, CSV_ContainsTaskDataRow)
{
    auto snap = makeTestSnapshot();
    PerformanceFormatter::formatCSV(snap, m_buf, kBufSize);

    std::string out(m_buf);
    // Task row: taskIndex=0, coreId=0, last=100, min=80, max=150
    EXPECT_NE(out.find("0,0,100,80,150,"), std::string::npos);
}

TEST_F(PerformanceFormatterTest, CSV_NullTerminated)
{
    auto snap = makeTestSnapshot();
    auto len  = PerformanceFormatter::formatCSV(snap, m_buf, kBufSize);

    EXPECT_EQ(m_buf[len], '\0');
}

// ===========================================================================
// Edge cases
// ===========================================================================

TEST_F(PerformanceFormatterTest, KeyValue_ZeroBuffer_ReturnsZero)
{
    auto snap = makeTestSnapshot();
    auto len  = PerformanceFormatter::formatKeyValue(snap, m_buf, 0);

    EXPECT_EQ(len, 0u);
}

TEST_F(PerformanceFormatterTest, CSV_ZeroBuffer_ReturnsZero)
{
    auto snap = makeTestSnapshot();
    auto len  = PerformanceFormatter::formatCSV(snap, m_buf, 0);

    EXPECT_EQ(len, 0u);
}

TEST_F(PerformanceFormatterTest, KeyValue_NullBuffer_ReturnsZero)
{
    auto snap = makeTestSnapshot();
    auto len  = PerformanceFormatter::formatKeyValue(snap, nullptr, kBufSize);

    EXPECT_EQ(len, 0u);
}

TEST_F(PerformanceFormatterTest, CSV_NullBuffer_ReturnsZero)
{
    auto snap = makeTestSnapshot();
    auto len  = PerformanceFormatter::formatCSV(snap, nullptr, kBufSize);

    EXPECT_EQ(len, 0u);
}

TEST_F(PerformanceFormatterTest, KeyValue_SmallBuffer_Truncates)
{
    auto snap = makeTestSnapshot();
    char small[32]{};
    auto len = PerformanceFormatter::formatKeyValue(snap, small, sizeof(small));

    EXPECT_GT(len, 0u);
    EXPECT_LT(len, sizeof(small));
    EXPECT_EQ(small[len], '\0');
}

TEST_F(PerformanceFormatterTest, KeyValue_NoTasks_OmitsTaskLines)
{
    PerformanceSnapshot snap{};
    snap.timestamp = 42;
    snap.coreCount = 1;
    snap.taskCount = 0;

    auto        len = PerformanceFormatter::formatKeyValue(snap, m_buf, kBufSize);
    std::string out(m_buf, len);

    EXPECT_NE(out.find("task_count=0\n"), std::string::npos);
    EXPECT_EQ(out.find("t0_"), std::string::npos);
}

TEST_F(PerformanceFormatterTest, KeyValue_MultiDigitTaskIndex)
{
    auto snap               = makeTestSnapshot();
    snap.taskCount          = 2;
    snap.tasks[1]           = snap.tasks[0];
    snap.tasks[1].taskIndex = 1;
    snap.tasks[1].coreId    = 1;

    PerformanceFormatter::formatKeyValue(snap, m_buf, kBufSize);
    std::string out(m_buf);

    EXPECT_NE(out.find("t1_core=1\n"), std::string::npos);
}
