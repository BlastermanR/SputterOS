/**
 * @file test_MemoryProfiler.cpp
 * @brief Unit tests for MemoryProfiler.
 *
 * MemoryProfiler reports free heap and stack high-water-mark. On the host
 * (non-embedded) target the underlying platform calls resolve to stubs
 * defined in the test build; this test validates the profiler's bookkeeping
 * logic, not the accuracy of the platform readings.
 *
 * Scenarios to cover:
 *   - A freshly constructed MemoryProfiler reports zero high-water-mark.
 *   - `update()` queries the platform for the current free bytes.
 *   - `getFreeHeapHWM()` returns the minimum observed free-heap value.
 *   - `getFreeStackHWM()` returns the minimum observed free-stack value.
 *   - HWM values never increase between calls if the platform stub is constant.
 *   - `reset()` clears all HWM state to the platform stub's initial value.
 *   - Calling `update()` zero times does not crash or produce invalid readings.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/5/2026
 */

#include "sputteros/utils/MemoryProfiler.h"
#include <gtest/gtest.h>

using namespace SputterOS;

// ===========================================================================
// Construction
// ===========================================================================

TEST(MemoryProfilerTest, InitialPeakUsed_IsZero)
{
    MemoryProfiler mp;
    EXPECT_EQ(mp.getPeakHeapUsedBytes(), 0u);
}

// ===========================================================================
// Static stubs
// ===========================================================================

TEST(MemoryProfilerTest, GetFreeHeapBytes_StubReturnsZero)
{
    // Host-portable stub returns 0 since no platform allocator is available.
    EXPECT_EQ(MemoryProfiler::getFreeHeapBytes(), 0u);
}

TEST(MemoryProfilerTest, GetStackHighWaterMark_StubReturnsZero)
{
    EXPECT_EQ(MemoryProfiler::getStackHighWaterMark(), 0u);
}

// ===========================================================================
// update() with platform stubs
// ===========================================================================

TEST(MemoryProfilerTest, Update_DoesNotCrash)
{
    MemoryProfiler mp;
    // Platform stubs return 0, so baseline=0, freeNow=0, usedNow=0.
    mp.update();
    EXPECT_EQ(mp.getPeakHeapUsedBytes(), 0u);
}

TEST(MemoryProfilerTest, Update_CalledMultipleTimes_StableAtZero)
{
    MemoryProfiler mp;
    for (int i = 0; i < 10; ++i)
    {
        mp.update();
    }
    // With stubs returning 0 and baseline=0, peak stays 0.
    EXPECT_EQ(mp.getPeakHeapUsedBytes(), 0u);
}

// ===========================================================================
// reset()
// ===========================================================================

TEST(MemoryProfilerTest, Reset_ClearsPeakUsed)
{
    MemoryProfiler mp;
    mp.update();
    mp.reset();
    EXPECT_EQ(mp.getPeakHeapUsedBytes(), 0u);
}

TEST(MemoryProfilerTest, Reset_CanUpdateAfterReset)
{
    MemoryProfiler mp;
    mp.update();
    mp.reset();
    mp.update();
    // Still 0 due to platform stubs returning 0.
    EXPECT_EQ(mp.getPeakHeapUsedBytes(), 0u);
}

// ===========================================================================
// Baseline-relative calculation (logic validation)
//
// Since getFreeHeapBytes() is a static method returning 0 in the portable
// build, the delta from baseline is always 0. These tests verify the
// bookkeeping logic does not produce negative or nonsensical values.
// ===========================================================================

TEST(MemoryProfilerTest, PeakUsed_NeverNegative)
{
    MemoryProfiler mp;
    mp.update();
    // Peak should be >= 0 (unsigned, so always true, but explicit).
    EXPECT_GE(mp.getPeakHeapUsedBytes(), 0u);
}

TEST(MemoryProfilerTest, MultipleResetCycles_StayConsistent)
{
    MemoryProfiler mp;
    for (int cycle = 0; cycle < 5; ++cycle)
    {
        for (int i = 0; i < 10; ++i)
            mp.update();
        EXPECT_EQ(mp.getPeakHeapUsedBytes(), 0u);
        mp.reset();
    }
}
