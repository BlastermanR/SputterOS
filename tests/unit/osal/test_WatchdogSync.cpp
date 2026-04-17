/**
 * @file test_WatchdogSync.cpp
 * @brief Unit tests for WatchdogSync inter-core heartbeat monitor.
 *
 * Validates kick/staleness semantics, timeout boundary conditions,
 * never-kicked core handling, and multi-core independence.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "sputteros/osal/sync/WatchdogSync.h"
#include <gtest/gtest.h>

using namespace SputterOS;

// ===========================================================================
// Initial state
// ===========================================================================

TEST(WatchdogSyncTest, InitialState_NeverKicked)
{
    WatchdogSync<2> wd;
    EXPECT_FALSE(wd.hasStarted(0));
    EXPECT_FALSE(wd.hasStarted(1));
    EXPECT_EQ(wd.lastKickTime(0), 0u);
    EXPECT_EQ(wd.lastKickTime(1), 0u);
}

TEST(WatchdogSyncTest, CoreCount_ReturnsTemplateParameter)
{
    EXPECT_EQ(WatchdogSync<2>::coreCount(), 2u);
    EXPECT_EQ(WatchdogSync<4>::coreCount(), 4u);
}

// ===========================================================================
// kick
// ===========================================================================

TEST(WatchdogSyncTest, Kick_SetsStartedFlag)
{
    WatchdogSync<2> wd;
    wd.kick(0, SputterMicros(1000));
    EXPECT_TRUE(wd.hasStarted(0));
    EXPECT_FALSE(wd.hasStarted(1)); // other core unchanged
}

TEST(WatchdogSyncTest, Kick_UpdatesTimestamp)
{
    WatchdogSync<2> wd;
    wd.kick(0, SputterMicros(5000));
    EXPECT_EQ(wd.lastKickTime(0), 5000u);

    wd.kick(0, SputterMicros(10000));
    EXPECT_EQ(wd.lastKickTime(0), 10000u);
}

// ===========================================================================
// isStale — never-kicked core
// ===========================================================================

TEST(WatchdogSyncTest, IsStale_NeverKicked_ReturnsFalse)
{
    WatchdogSync<2> wd;
    // Core 0 has never kicked — startup grace; not considered stale.
    EXPECT_FALSE(wd.isStale(0, SputterMicros(999999), SputterMicros(100)));
}

// ===========================================================================
// isStale — fresh kick
// ===========================================================================

TEST(WatchdogSyncTest, IsStale_FreshKick_ReturnsFalse)
{
    WatchdogSync<2> wd;
    wd.kick(0, SputterMicros(1000));

    // Check immediately: delta = 1000 - 1000 = 0, timeout = 500 → not stale.
    EXPECT_FALSE(wd.isStale(0, SputterMicros(1000), SputterMicros(500)));
}

TEST(WatchdogSyncTest, IsStale_WithinTimeout_ReturnsFalse)
{
    WatchdogSync<2> wd;
    wd.kick(0, SputterMicros(1000));

    // delta = 1400 - 1000 = 400, timeout = 500 → not stale.
    EXPECT_FALSE(wd.isStale(0, SputterMicros(1400), SputterMicros(500)));
}

// ===========================================================================
// isStale — boundary
// ===========================================================================

TEST(WatchdogSyncTest, IsStale_ExactlyAtTimeout_ReturnsFalse)
{
    WatchdogSync<2> wd;
    wd.kick(0, SputterMicros(1000));

    // delta = 1500 - 1000 = 500, timeout = 500 → NOT stale (> not >=).
    EXPECT_FALSE(wd.isStale(0, SputterMicros(1500), SputterMicros(500)));
}

TEST(WatchdogSyncTest, IsStale_OneOverTimeout_ReturnsTrue)
{
    WatchdogSync<2> wd;
    wd.kick(0, SputterMicros(1000));

    // delta = 1501 - 1000 = 501, timeout = 500 → stale.
    EXPECT_TRUE(wd.isStale(0, SputterMicros(1501), SputterMicros(500)));
}

// ===========================================================================
// isStale — exceeded timeout
// ===========================================================================

TEST(WatchdogSyncTest, IsStale_LongOverdue_ReturnsTrue)
{
    WatchdogSync<2> wd;
    wd.kick(0, SputterMicros(1000));

    EXPECT_TRUE(wd.isStale(0, SputterMicros(1000000), SputterMicros(500)));
}

// ===========================================================================
// isStale — re-kick resets staleness
// ===========================================================================

TEST(WatchdogSyncTest, IsStale_ReKick_ResetsToFresh)
{
    WatchdogSync<2> wd;
    wd.kick(0, SputterMicros(1000));

    // Goes stale.
    EXPECT_TRUE(wd.isStale(0, SputterMicros(2000), SputterMicros(500)));

    // Re-kick.
    wd.kick(0, SputterMicros(2000));

    // Now fresh again.
    EXPECT_FALSE(wd.isStale(0, SputterMicros(2200), SputterMicros(500)));
}

// ===========================================================================
// Multi-core independence
// ===========================================================================

TEST(WatchdogSyncTest, MultiCore_IndependentStaleness)
{
    WatchdogSync<2> wd;

    wd.kick(0, SputterMicros(1000));
    wd.kick(1, SputterMicros(5000));

    // At time 6000: core 0 delta = 5000 (stale), core 1 delta = 1000 (fresh).
    EXPECT_TRUE(wd.isStale(0, SputterMicros(6000), SputterMicros(2000)));
    EXPECT_FALSE(wd.isStale(1, SputterMicros(6000), SputterMicros(2000)));
}

TEST(WatchdogSyncTest, MultiCore_KickOneDoesNotAffectOther)
{
    WatchdogSync<2> wd;

    wd.kick(0, SputterMicros(1000));
    EXPECT_TRUE(wd.hasStarted(0));
    EXPECT_FALSE(wd.hasStarted(1));
    EXPECT_EQ(wd.lastKickTime(1), 0u);
}

// ===========================================================================
// Single-core instantiation
// ===========================================================================

TEST(WatchdogSyncTest, SingleCore_CompilationAndBasicOperation)
{
    WatchdogSync<1> wd;
    EXPECT_EQ(WatchdogSync<1>::coreCount(), 1u);

    wd.kick(0, SputterMicros(100));
    EXPECT_TRUE(wd.hasStarted(0));
    EXPECT_FALSE(wd.isStale(0, SputterMicros(200), SputterMicros(500)));
}
