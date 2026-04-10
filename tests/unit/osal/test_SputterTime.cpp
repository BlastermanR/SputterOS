/**
 * @file test_SputterTime.cpp
 * @brief Unit tests for SputterTime types and SystemTimer.
 *
 * Validates clock source injection, `nowMicros()` forwarding, the
 * `hasClockSource()` predicate, and the nholthaus/units convenience
 * getters (microseconds, milliseconds, seconds).
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "sputteros/osal/SputterTime.h"
#include <gtest/gtest.h>

using namespace SputterOS;

// ===========================================================================
// Test clock sources
// ===========================================================================

namespace
{

static uint64_t s_fakeTime = 0;

uint64_t fakeClockZero() { return 0; }
uint64_t fakeClockFixed() { return 1000000; } // 1 second
uint64_t fakeClockDynamic() { return s_fakeTime; }

} // namespace

// ===========================================================================
// hasClockSource
// ===========================================================================

TEST(SystemTimerTest, DefaultConstruction_NoClockSource)
{
    SystemTimer timer;
    EXPECT_FALSE(timer.hasClockSource());
}

TEST(SystemTimerTest, SetClockSource_HasClockSourceReturnsTrue)
{
    SystemTimer timer;
    timer.setClockSource(fakeClockZero);
    EXPECT_TRUE(timer.hasClockSource());
}

TEST(SystemTimerTest, ExplicitConstruction_WithClockSource)
{
    SystemTimer timer(fakeClockFixed);
    EXPECT_TRUE(timer.hasClockSource());
}

// ===========================================================================
// nowMicros
// ===========================================================================

TEST(SystemTimerTest, NowMicros_NoClockSource_ReturnsZero)
{
    SystemTimer timer;
    EXPECT_EQ(timer.nowMicros(), 0u);
}

TEST(SystemTimerTest, NowMicros_ReturnsInjectedValue)
{
    SystemTimer timer(fakeClockFixed);
    EXPECT_EQ(timer.nowMicros(), 1000000u);
}

TEST(SystemTimerTest, NowMicros_TracksDynamicClock)
{
    s_fakeTime = 500;
    SystemTimer timer(fakeClockDynamic);
    EXPECT_EQ(timer.nowMicros(), 500u);

    s_fakeTime = 999999;
    EXPECT_EQ(timer.nowMicros(), 999999u);
}

// ===========================================================================
// setClockSource replaces existing source
// ===========================================================================

TEST(SystemTimerTest, SetClockSource_ReplacesExisting)
{
    SystemTimer timer(fakeClockZero);
    EXPECT_EQ(timer.nowMicros(), 0u);

    timer.setClockSource(fakeClockFixed);
    EXPECT_EQ(timer.nowMicros(), 1000000u);
}

// ===========================================================================
// Units convenience getters
// ===========================================================================

TEST(SystemTimerTest, Microseconds_ConvertsCorrectly)
{
    SystemTimer timer(fakeClockFixed); // 1,000,000 µs
    auto        us = timer.microseconds();
    EXPECT_DOUBLE_EQ(us.value(), 1000000.0);
}

TEST(SystemTimerTest, Milliseconds_ConvertsCorrectly)
{
    SystemTimer timer(fakeClockFixed); // 1,000,000 µs = 1000 ms
    auto        ms = timer.milliseconds();
    EXPECT_DOUBLE_EQ(ms.value(), 1000.0);
}

TEST(SystemTimerTest, Seconds_ConvertsCorrectly)
{
    SystemTimer timer(fakeClockFixed); // 1,000,000 µs = 1.0 s
    auto        s = timer.seconds();
    EXPECT_DOUBLE_EQ(s.value(), 1.0);
}

// ===========================================================================
// Static conversion helpers
// ===========================================================================

TEST(SystemTimerTest, StaticToMicroseconds)
{
    auto us = SystemTimer::toMicroseconds(5000);
    EXPECT_DOUBLE_EQ(us.value(), 5000.0);
}

TEST(SystemTimerTest, StaticToMilliseconds)
{
    auto ms = SystemTimer::toMilliseconds(2500000);
    EXPECT_DOUBLE_EQ(ms.value(), 2500.0);
}

TEST(SystemTimerTest, StaticToSeconds)
{
    auto s = SystemTimer::toSeconds(3000000);
    EXPECT_DOUBLE_EQ(s.value(), 3.0);
}

// ===========================================================================
// SputterMicros type alias
// ===========================================================================

TEST(SputterTimeTest, SputterMicros_IsUint64)
{
    static_assert(std::is_same_v<SputterMicros, uint64_t>, "SputterMicros must be uint64_t");
}

// ===========================================================================
// MicrosecondSource type alias
// ===========================================================================

TEST(SputterTimeTest, MicrosecondSource_AcceptsFunctionPointer)
{
    MicrosecondSource src = fakeClockFixed;
    EXPECT_EQ(src(), 1000000u);
}
