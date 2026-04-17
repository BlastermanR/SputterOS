/**
 * @file test_LockFreeQueue.cpp
 * @brief Unit tests for LockFreeQueue SPSC ring buffer.
 *
 * Validates push/pop semantics, full/empty boundary conditions, index
 * wraparound, size accuracy, and the ICommandProducer / ICommandConsumer
 * interface views. The queue is accessed through `System<Cfg>::commandQueue()`
 * because `LockFreeQueue` has a private constructor (friend-only).
 *
 * Each test uses a unique `QueueTestCfg<N>` type to get an independent
 * set of `System<Cfg>` statics with no cross-test interference.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "KernelTestAccess.h"
#include "sputteros/kernel/System.h"
#include "sputteros/osal/sync/ICommandConsumer.h"
#include "sputteros/osal/sync/ICommandProducer.h"
#include <gtest/gtest.h>

using namespace SputterOS;

// ===========================================================================
// Per-test isolated configuration
// ===========================================================================

template <int N, std::size_t Cap = 4> struct QueueTestCfg
{
    enum class State : uint8_t
    {
        IDLE = 0
    };
    enum class CmdID : uint8_t
    {
        NONE = 0,
        SET  = 1
    };
    struct Command
    {
        CmdID   id;
        uint8_t targetDevice;
        float   value;
    };
    static constexpr int         kMaxCommandsPerTick = 8;
    static constexpr uint8_t     kMaxValidCommandID  = 1;
    static constexpr std::size_t kCoreCount          = 1;
    static constexpr std::size_t kQueueCapacity      = Cap;
};

// Helper to make a command with a given value for identification.
template <typename Cfg> typename Cfg::Command makeCmd(float val)
{
    typename Cfg::Command cmd{};
    cmd.id           = Cfg::CmdID::SET;
    cmd.targetDevice = 0;
    cmd.value        = val;
    return cmd;
}

// ===========================================================================
// Push / Pop fundamentals
// ===========================================================================

TEST(LockFreeQueue, PushPop_SingleElement_RoundTrip)
{
    using Cfg = QueueTestCfg<1>;
    Kernel::KernelTestAccess::resetSystem<Cfg>();
    auto &q = System<Cfg>::commandQueue();

    auto cmd = makeCmd<Cfg>(42.0f);
    EXPECT_TRUE(q.try_push(cmd));
    EXPECT_EQ(q.size(), 1u);

    typename Cfg::Command out{};
    EXPECT_TRUE(q.try_pop(out));
    EXPECT_FLOAT_EQ(out.value, 42.0f);
    EXPECT_EQ(q.size(), 0u);
}

TEST(LockFreeQueue, Pop_EmptyQueue_ReturnsFalse)
{
    using Cfg = QueueTestCfg<2>;
    Kernel::KernelTestAccess::resetSystem<Cfg>();
    auto &q = System<Cfg>::commandQueue();

    typename Cfg::Command out{};
    EXPECT_FALSE(q.try_pop(out));
}

TEST(LockFreeQueue, Push_FullQueue_ReturnsFalse)
{
    using Cfg = QueueTestCfg<3, 3>;
    Kernel::KernelTestAccess::resetSystem<Cfg>();
    auto &q = System<Cfg>::commandQueue();

    // Fill to capacity (3 slots).
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_TRUE(q.try_push(makeCmd<Cfg>(static_cast<float>(i))));
    }
    EXPECT_EQ(q.size(), 3u);

    // One more should be rejected.
    EXPECT_FALSE(q.try_push(makeCmd<Cfg>(99.0f)));
    EXPECT_EQ(q.size(), 3u);
}

// ===========================================================================
// Size accuracy
// ===========================================================================

TEST(LockFreeQueue, Size_TracksCorrectly)
{
    using Cfg = QueueTestCfg<4, 8>;
    Kernel::KernelTestAccess::resetSystem<Cfg>();
    auto &q = System<Cfg>::commandQueue();

    EXPECT_EQ(q.size(), 0u);

    q.try_push(makeCmd<Cfg>(1.0f));
    q.try_push(makeCmd<Cfg>(2.0f));
    EXPECT_EQ(q.size(), 2u);

    typename Cfg::Command out{};
    q.try_pop(out);
    EXPECT_EQ(q.size(), 1u);

    q.try_pop(out);
    EXPECT_EQ(q.size(), 0u);
}

// ===========================================================================
// Capacity
// ===========================================================================

TEST(LockFreeQueue, Capacity_ReturnsTemplateParameter)
{
    using Cfg = QueueTestCfg<5, 16>;
    auto &q   = System<Cfg>::commandQueue();
    EXPECT_EQ(q.capacity(), 16u);
}

// ===========================================================================
// FIFO order
// ===========================================================================

TEST(LockFreeQueue, FIFOOrder_PreservedAcrossPushPop)
{
    using Cfg = QueueTestCfg<6, 8>;
    Kernel::KernelTestAccess::resetSystem<Cfg>();
    auto &q = System<Cfg>::commandQueue();

    for (int i = 0; i < 5; ++i)
    {
        q.try_push(makeCmd<Cfg>(static_cast<float>(i)));
    }

    for (int i = 0; i < 5; ++i)
    {
        typename Cfg::Command out{};
        ASSERT_TRUE(q.try_pop(out));
        EXPECT_FLOAT_EQ(out.value, static_cast<float>(i));
    }
}

// ===========================================================================
// Index wraparound
// ===========================================================================

TEST(LockFreeQueue, Wraparound_IndicesWrapCorrectly)
{
    using Cfg = QueueTestCfg<7, 3>;
    Kernel::KernelTestAccess::resetSystem<Cfg>();
    auto &q = System<Cfg>::commandQueue();

    // Push 3, pop 3 (advances head and tail past kSlots boundary).
    for (int i = 0; i < 3; ++i)
    {
        ASSERT_TRUE(q.try_push(makeCmd<Cfg>(static_cast<float>(i))));
    }
    typename Cfg::Command out{};
    for (int i = 0; i < 3; ++i)
    {
        ASSERT_TRUE(q.try_pop(out));
    }
    EXPECT_EQ(q.size(), 0u);

    // Push 3 more — forces head/tail to wrap around kSlots.
    for (int i = 10; i < 13; ++i)
    {
        ASSERT_TRUE(q.try_push(makeCmd<Cfg>(static_cast<float>(i))));
    }
    EXPECT_EQ(q.size(), 3u);

    // Verify FIFO order after wrap.
    for (int i = 10; i < 13; ++i)
    {
        ASSERT_TRUE(q.try_pop(out));
        EXPECT_FLOAT_EQ(out.value, static_cast<float>(i));
    }
}

// ===========================================================================
// Clear
// ===========================================================================

TEST(LockFreeQueue, Clear_ResetsSize)
{
    using Cfg = QueueTestCfg<8, 4>;
    Kernel::KernelTestAccess::resetSystem<Cfg>();
    auto &q = System<Cfg>::commandQueue();

    q.try_push(makeCmd<Cfg>(1.0f));
    q.try_push(makeCmd<Cfg>(2.0f));
    EXPECT_EQ(q.size(), 2u);

    q.clear();
    EXPECT_EQ(q.size(), 0u);

    // Pop should fail after clear.
    typename Cfg::Command out{};
    EXPECT_FALSE(q.try_pop(out));
}

// ===========================================================================
// Alternating push/pop stress
// ===========================================================================

TEST(LockFreeQueue, AlternatingPushPop_100Cycles)
{
    using Cfg = QueueTestCfg<9, 4>;
    Kernel::KernelTestAccess::resetSystem<Cfg>();
    auto &q = System<Cfg>::commandQueue();

    for (int i = 0; i < 100; ++i)
    {
        ASSERT_TRUE(q.try_push(makeCmd<Cfg>(static_cast<float>(i))));
        typename Cfg::Command out{};
        ASSERT_TRUE(q.try_pop(out));
        EXPECT_FLOAT_EQ(out.value, static_cast<float>(i));
    }
    EXPECT_EQ(q.size(), 0u);
}

// ===========================================================================
// Interface conformance (push via ICommandProducer, pop via ICommandConsumer)
// ===========================================================================

TEST(LockFreeQueue, Interface_PushViaProducer_PopViaConsumer)
{
    using Cfg = QueueTestCfg<10, 8>;
    Kernel::KernelTestAccess::resetSystem<Cfg>();

    ICommandProducer<Cfg> &producer = System<Cfg>::commandQueue();
    ICommandConsumer<Cfg> &consumer = System<Cfg>::commandQueue();

    auto cmd = makeCmd<Cfg>(7.5f);
    EXPECT_TRUE(producer.try_push(cmd));

    typename Cfg::Command out{};
    EXPECT_TRUE(consumer.try_pop(out));
    EXPECT_FLOAT_EQ(out.value, 7.5f);
}

// ===========================================================================
// push() with timeout (IMessageQueue conformance — timeout is ignored)
// ===========================================================================

TEST(LockFreeQueue, Push_WithTimeout_BehavesLikeTryPush)
{
    using Cfg = QueueTestCfg<11, 2>;
    Kernel::KernelTestAccess::resetSystem<Cfg>();
    auto &q = System<Cfg>::commandQueue();

    auto cmd = makeCmd<Cfg>(1.0f);
    EXPECT_TRUE(q.push(cmd, 100));

    typename Cfg::Command out{};
    EXPECT_TRUE(q.pop(out, 100));
    EXPECT_FLOAT_EQ(out.value, 1.0f);
}

// ===========================================================================
// Fill-then-empty cycle
// ===========================================================================

TEST(LockFreeQueue, FillThenEmpty_ReusableAfterDrain)
{
    using Cfg = QueueTestCfg<12, 4>;
    Kernel::KernelTestAccess::resetSystem<Cfg>();
    auto &q = System<Cfg>::commandQueue();

    // Fill.
    for (int i = 0; i < 4; ++i)
        ASSERT_TRUE(q.try_push(makeCmd<Cfg>(static_cast<float>(i))));
    EXPECT_FALSE(q.try_push(makeCmd<Cfg>(99.0f))); // full

    // Drain.
    typename Cfg::Command out{};
    for (int i = 0; i < 4; ++i)
        ASSERT_TRUE(q.try_pop(out));
    EXPECT_FALSE(q.try_pop(out)); // empty

    // Re-fill and verify — ensures indices reset correctly after full cycle.
    for (int i = 100; i < 104; ++i)
        ASSERT_TRUE(q.try_push(makeCmd<Cfg>(static_cast<float>(i))));
    for (int i = 100; i < 104; ++i)
    {
        ASSERT_TRUE(q.try_pop(out));
        EXPECT_FLOAT_EQ(out.value, static_cast<float>(i));
    }
}
