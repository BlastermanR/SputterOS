/**
 * @file test_AtomicDoubleBuffer.cpp
 * @brief Unit tests for AtomicDoubleBuffer wait-free latest-value buffer.
 *
 * Validates round-trip fidelity, latest-value semantics, default
 * initialisation, multi-word struct integrity, and the CachePolicy
 * flush/invalidate hook.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/13/2026
 */

#include "sputteros/osal/sync/AtomicDoubleBuffer.h"
#include <gtest/gtest.h>
#include <type_traits>

using namespace SputterOS;

// ===========================================================================
// Helpers — test value types
// ===========================================================================

/// @brief 24-byte POD struct for multi-word round-trip tests.
struct SmallStruct
{
    float    x{};
    float    y{};
    float    z{};
    uint32_t seq{};
    uint32_t flags{};
    uint32_t pad{};

    bool operator==(const SmallStruct &o) const
    {
        return x == o.x && y == o.y && z == o.z && seq == o.seq && flags == o.flags && pad == o.pad;
    }
};
static_assert(sizeof(SmallStruct) == 24, "SmallStruct must be 24 bytes");

/// @brief 48-byte POD struct for larger multi-word round-trip tests.
struct LargeStruct
{
    double   a{};
    double   b{};
    double   c{};
    double   d{};
    uint32_t w{};
    uint32_t x{};
    uint32_t y{};
    uint32_t z{};

    bool operator==(const LargeStruct &o) const
    {
        return a == o.a && b == o.b && c == o.c && d == o.d && w == o.w && x == o.x && y == o.y && z == o.z;
    }
};
static_assert(sizeof(LargeStruct) == 48, "LargeStruct must be 48 bytes");

// ===========================================================================
// Helper — tracking CachePolicy
// ===========================================================================

/**
 * @brief CachePolicy that records the number of flush/invalidate calls
 *        into externally supplied counters.
 *
 * @note Counters are passed via template so each test gets its own state
 *       without dynamic allocation.
 */
struct TrackingPolicyCounters
{
    int flushCount{0};
    int invalidateCount{0};
};

template <TrackingPolicyCounters *Counters> struct TrackingCachePolicy
{
    static void flushBuffer(const void * /*addr*/, std::size_t /*bytes*/) { ++Counters->flushCount; }

    static void invalidateBuffer(const void * /*addr*/, std::size_t /*bytes*/) { ++Counters->invalidateCount; }
};

// ===========================================================================
// Basic read / write — scalar type
// ===========================================================================

TEST(AtomicDoubleBufferTest, WriteRead_SingleValue_RoundTrips)
{
    AtomicDoubleBuffer<int> buf;
    buf.write(42);
    EXPECT_EQ(buf.read(), 42);
}

TEST(AtomicDoubleBufferTest, Write_Twice_ReadReturnsLatest)
{
    AtomicDoubleBuffer<int> buf;
    buf.write(1);
    buf.write(99);
    EXPECT_EQ(buf.read(), 99);
}

TEST(AtomicDoubleBufferTest, DefaultConstruct_ReadsZeroed)
{
    AtomicDoubleBuffer<int> buf;
    EXPECT_EQ(buf.read(), 0);
}

// ===========================================================================
// Multi-word struct integrity
// ===========================================================================

TEST(AtomicDoubleBufferTest, MultiWordStruct_24Byte_Integrity)
{
    AtomicDoubleBuffer<SmallStruct> buf;

    const SmallStruct in{1.0f, 2.0f, 3.0f, 7u, 0xDEADu, 0xBEEFu};
    buf.write(in);
    const SmallStruct out = buf.read();

    EXPECT_EQ(out, in);
}

TEST(AtomicDoubleBufferTest, MultiWordStruct_48Byte_Integrity)
{
    AtomicDoubleBuffer<LargeStruct> buf;

    const LargeStruct in{1.1, 2.2, 3.3, 4.4, 10u, 20u, 30u, 40u};
    buf.write(in);
    const LargeStruct out = buf.read();

    EXPECT_EQ(out, in);
}

TEST(AtomicDoubleBufferTest, MultiWordStruct_MultipleWrites_ReturnsLatest)
{
    AtomicDoubleBuffer<SmallStruct> buf;

    const SmallStruct first{0.0f, 0.0f, 0.0f, 1u, 0u, 0u};
    const SmallStruct second{9.0f, 8.0f, 7.0f, 2u, 0xFFu, 0xAAu};
    buf.write(first);
    buf.write(second);

    EXPECT_EQ(buf.read(), second);
}

// ===========================================================================
// CachePolicy hook — flush called on write
// ===========================================================================

TEST(AtomicDoubleBufferTest, CustomCachePolicy_FlushCalledOnWrite)
{
    static TrackingPolicyCounters counters;
    counters = {};

    using Policy = TrackingCachePolicy<&counters>;
    AtomicDoubleBuffer<int, Policy> buf;

    buf.write(1);
    EXPECT_EQ(counters.flushCount, 1);
    EXPECT_EQ(counters.invalidateCount, 0);

    buf.write(2);
    EXPECT_EQ(counters.flushCount, 2);
}

// ===========================================================================
// CachePolicy hook — invalidate called on read
// ===========================================================================

TEST(AtomicDoubleBufferTest, CustomCachePolicy_InvalidateCalledOnRead)
{
    static TrackingPolicyCounters counters;
    counters = {};

    using Policy = TrackingCachePolicy<&counters>;
    AtomicDoubleBuffer<int, Policy> buf;

    buf.write(5);
    counters.invalidateCount = 0; // reset after write side-effects

    (void)buf.read();
    EXPECT_EQ(counters.invalidateCount, 1);
    EXPECT_EQ(counters.flushCount, 1); // only 1 from the write above

    (void)buf.read();
    EXPECT_EQ(counters.invalidateCount, 2);
}

// ===========================================================================
// Non-copyable guarantee
// ===========================================================================

TEST(AtomicDoubleBufferTest, NonCopyable_CompileTimeEnforced)
{
    using Buf = AtomicDoubleBuffer<int>;
    EXPECT_FALSE(std::is_copy_constructible_v<Buf>);
    EXPECT_FALSE(std::is_copy_assignable_v<Buf>);
}
