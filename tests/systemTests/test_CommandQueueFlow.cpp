/**
 * @file test_CommandQueueFlow.cpp
 * @brief System tests for inter-core command delivery via LockFreeQueue.
 *
 * Tests the real SPSC ring buffer through the System singleton API:
 * - Push/pop delivers correct command
 * - FIFO ordering preserved
 * - Full queue rejects pushes gracefully
 * - Empty queue returns false on pop
 */

#include "SystemTestConfig.h"
#include "SystemTestHelpers.h"

#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/System.h"

#include "../../unit/mocks/KernelTestAccess.h"

#include <gtest/gtest.h>

using namespace SputterOS;
using namespace SputterOS::SystemTests;

using Cfg = SingleCoreConfig;
using Cmd = Cfg::Command;

class CommandQueueFlow : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Infrastructure-only mode — no kernel tasks. Just the queue.
        SystemBuilder<Cfg> builder(nullptr, nullptr, 0);
        builder.core(0).addTask(&userTask);
        ASSERT_TRUE(builder.build());
    }

    void TearDown() override { Kernel::KernelTestAccess::resetSystem<Cfg>(); }

    InstrumentedTask userTask;
};

TEST_F(CommandQueueFlow, PushPopDeliversCommand)
{
    auto &queue = System<Cfg>::commandQueue();

    Cmd sent{Cfg::CmdID::SET_FLOW, 2, 3.14f};
    EXPECT_TRUE(queue.try_push(sent));

    Cmd received{};
    EXPECT_TRUE(queue.try_pop(received));

    EXPECT_EQ(static_cast<uint8_t>(received.id), static_cast<uint8_t>(Cfg::CmdID::SET_FLOW));
    EXPECT_EQ(received.targetDevice, 2);
    EXPECT_FLOAT_EQ(received.value, 3.14f);
}

TEST_F(CommandQueueFlow, FIFOOrderPreserved)
{
    auto &queue = System<Cfg>::commandQueue();

    for (uint8_t i = 0; i < 5; ++i)
    {
        Cmd cmd{Cfg::CmdID::SET_STATE, i, static_cast<float>(i * 10)};
        ASSERT_TRUE(queue.try_push(cmd));
    }

    for (uint8_t i = 0; i < 5; ++i)
    {
        Cmd cmd{};
        ASSERT_TRUE(queue.try_pop(cmd));
        EXPECT_EQ(cmd.targetDevice, i) << "FIFO violation at index " << (int)i;
        EXPECT_FLOAT_EQ(cmd.value, static_cast<float>(i * 10));
    }
}

TEST_F(CommandQueueFlow, FullQueueRejectsPush)
{
    auto &queue = System<Cfg>::commandQueue();

    // Fill the queue to capacity (kQueueCapacity = 16)
    for (std::size_t i = 0; i < Cfg::kQueueCapacity; ++i)
    {
        Cmd cmd{Cfg::CmdID::SET_STATE, 0, static_cast<float>(i)};
        ASSERT_TRUE(queue.try_push(cmd)) << "Push failed at index " << i;
    }

    // Next push should fail
    Cmd overflow{Cfg::CmdID::ABORT_PROCESS, 0, 0.0f};
    EXPECT_FALSE(queue.try_push(overflow));
}

TEST_F(CommandQueueFlow, EmptyQueueReturnsFalse)
{
    auto &queue = System<Cfg>::commandQueue();

    Cmd cmd{};
    EXPECT_FALSE(queue.try_pop(cmd));
}

TEST_F(CommandQueueFlow, QueueSizeReflectsContent)
{
    auto &queue = System<Cfg>::commandQueue();

    EXPECT_EQ(queue.size(), 0u);

    Cmd cmd{Cfg::CmdID::SET_STATE, 0, 1.0f};
    queue.try_push(cmd);
    EXPECT_EQ(queue.size(), 1u);

    queue.try_push(cmd);
    EXPECT_EQ(queue.size(), 2u);

    queue.try_pop(cmd);
    EXPECT_EQ(queue.size(), 1u);
}
