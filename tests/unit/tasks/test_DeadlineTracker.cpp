/**
 * @file test_DeadlineTracker.cpp
 * @brief Unit tests for Kernel::DeadlineTracker periodic deadline tracker.
 *
 * Validates initialization, due-check, period advancement (including
 * missed-period skipping), and time-until-next computation.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "sputteros/kernel/metrics/DeadlineTracker.h"
#include <gtest/gtest.h>

using namespace SputterOS;
using namespace SputterOS::Kernel;

// ===========================================================================
// Init
// ===========================================================================

TEST(DeadlineTrackerTest, Init_SetsNextActivation)
{
    DeadlineTracker dt;
    dt.periodUs      = 10000; // 10 ms
    dt.phaseOffsetUs = 500;

    dt.init(1000);

    EXPECT_EQ(dt.nextActivation, 1500u); // startTime + phaseOffset
}

TEST(DeadlineTrackerTest, Init_ZeroPhaseOffset)
{
    DeadlineTracker dt;
    dt.periodUs      = 10000;
    dt.phaseOffsetUs = 0;

    dt.init(2000);

    EXPECT_EQ(dt.nextActivation, 2000u);
}

// ===========================================================================
// isDue
// ===========================================================================

TEST(DeadlineTrackerTest, IsDue_BeforeActivation_ReturnsFalse)
{
    DeadlineTracker dt;
    dt.periodUs      = 10000;
    dt.phaseOffsetUs = 0;
    dt.init(1000);

    EXPECT_FALSE(dt.isDue(999));
}

TEST(DeadlineTrackerTest, IsDue_ExactlyAtActivation_ReturnsTrue)
{
    DeadlineTracker dt;
    dt.periodUs      = 10000;
    dt.phaseOffsetUs = 0;
    dt.init(1000);

    EXPECT_TRUE(dt.isDue(1000));
}

TEST(DeadlineTrackerTest, IsDue_AfterActivation_ReturnsTrue)
{
    DeadlineTracker dt;
    dt.periodUs      = 10000;
    dt.phaseOffsetUs = 0;
    dt.init(1000);

    EXPECT_TRUE(dt.isDue(5000));
}

// ===========================================================================
// advance
// ===========================================================================

TEST(DeadlineTrackerTest, Advance_SinglePeriod)
{
    DeadlineTracker dt;
    dt.periodUs      = 10000;
    dt.phaseOffsetUs = 0;
    dt.init(1000); // nextActivation = 1000

    dt.advance(1000); // now == nextActivation → advance once

    EXPECT_EQ(dt.nextActivation, 11000u); // 1000 + 10000
}

TEST(DeadlineTrackerTest, Advance_SkipsMissedPeriods)
{
    DeadlineTracker dt;
    dt.periodUs      = 10000;
    dt.phaseOffsetUs = 0;
    dt.init(0); // nextActivation = 0

    // Simulate being 25000 µs late — should skip past 10000 and 20000.
    dt.advance(25000);

    EXPECT_EQ(dt.nextActivation, 30000u);
    EXPECT_GT(dt.nextActivation, 25000u);
}

TEST(DeadlineTrackerTest, Advance_ExactMultipleOfPeriod)
{
    DeadlineTracker dt;
    dt.periodUs      = 5000;
    dt.phaseOffsetUs = 0;
    dt.init(0); // nextActivation = 0

    // now == 15000 is exactly 3 periods.
    dt.advance(15000);

    // Should skip past 5000, 10000, 15000 → land on 20000.
    EXPECT_EQ(dt.nextActivation, 20000u);
}

// ===========================================================================
// timeUntilNext
// ===========================================================================

TEST(DeadlineTrackerTest, TimeUntilNext_BeforeActivation)
{
    DeadlineTracker dt;
    dt.periodUs      = 10000;
    dt.phaseOffsetUs = 0;
    dt.init(5000);

    EXPECT_EQ(dt.timeUntilNext(3000), 2000u);
}

TEST(DeadlineTrackerTest, TimeUntilNext_AtActivation_ReturnsZero)
{
    DeadlineTracker dt;
    dt.periodUs      = 10000;
    dt.phaseOffsetUs = 0;
    dt.init(5000);

    EXPECT_EQ(dt.timeUntilNext(5000), 0u);
}

TEST(DeadlineTrackerTest, TimeUntilNext_PastActivation_ReturnsZero)
{
    DeadlineTracker dt;
    dt.periodUs      = 10000;
    dt.phaseOffsetUs = 0;
    dt.init(5000);

    EXPECT_EQ(dt.timeUntilNext(8000), 0u);
}
