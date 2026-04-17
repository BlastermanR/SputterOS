/**
 * @file test_CommsTask.cpp
 * @brief Unit tests for ScheduledCommsTask<Cfg>.
 *
 * Feeds synthetic byte streams through MockStreamReader and verifies
 * that valid commands are pushed to MockMessageQueue<Cfg>.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */

#include "KernelTestAccess.h"
#include "MockMessageQueue.h"
#include "MockStreamReader.h"
#include "TestConfig.h"
#include "sputteros/osal/SputterTime.h"
#include <cstring>
#include <gtest/gtest.h>

using namespace SputterOS;
using namespace SputterOS::Kernel;
using Cfg           = TestConfig;
using CmdID         = Cfg::CmdID;
using CommandStruct = Cfg::Command;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArrayArgument;

// ===========================================================================
// Helpers
// ===========================================================================

// Configures the mock stream to serve the given byte string on the next read.
static void expectBytes(NiceMock<MockStreamReader> &stream, const char *data)
{
    const std::size_t len = std::strlen(data);
    ON_CALL(stream, available()).WillByDefault(Return(len));
    ON_CALL(stream, read(::testing::_, ::testing::_))
        .WillByDefault(DoAll(
            SetArrayArgument<0>(reinterpret_cast<const uint8_t *>(data), reinterpret_cast<const uint8_t *>(data) + len),
            Return(len)));
}

// ===========================================================================
// Fixture
// ===========================================================================

class CommsTaskTest : public ::testing::Test
{
  protected:
    NiceMock<MockStreamReader>      stream;
    NiceMock<MockMessageQueue<Cfg>> queue;
    ScheduledCommsTask<Cfg>         task = KernelTestAccess::makeCommsTask<Cfg>(&stream, &queue);

    void SetUp() override
    {
        // Default: empty stream, queue accepts everything
        ON_CALL(stream, available()).WillByDefault(Return(0));
        ON_CALL(queue, try_push(::testing::_)).WillByDefault(Return(true));
    }
};

// ---------------------------------------------------------------------------
// validateDependencies
// ---------------------------------------------------------------------------

TEST_F(CommsTaskTest, ValidateDependencies_AllNonNull_ReturnsTrue) { EXPECT_TRUE(task.validateDependencies()); }

TEST_F(CommsTaskTest, ValidateDependencies_NullStream_ReturnsFalse)
{
    ScheduledCommsTask<Cfg> bad = KernelTestAccess::makeCommsTask<Cfg>(nullptr, &queue);
    EXPECT_FALSE(bad.validateDependencies());
}

TEST_F(CommsTaskTest, ValidateDependencies_NullQueue_ReturnsFalse)
{
    ScheduledCommsTask<Cfg> bad = KernelTestAccess::makeCommsTask<Cfg>(&stream, nullptr);
    EXPECT_FALSE(bad.validateDependencies());
}

// ---------------------------------------------------------------------------
// Empty stream â€” no push
// ---------------------------------------------------------------------------

TEST_F(CommsTaskTest, EmptyStream_NoPushToQueue)
{
    ON_CALL(stream, available()).WillByDefault(Return(0));
    EXPECT_CALL(queue, try_push(::testing::_)).Times(0);
    task.tick(SputterMicros(0));
}

// ---------------------------------------------------------------------------
// Valid complete command â€” pushed to queue
// ---------------------------------------------------------------------------

TEST_F(CommsTaskTest, ValidCommand_PushedToQueue)
{
    expectBytes(stream, "1 0 50.0\n");
    EXPECT_CALL(queue, try_push(::testing::_)).Times(1);
    task.tick(SputterMicros(0));
}

TEST_F(CommsTaskTest, ValidCommand_CorrectCommandIDPushed)
{
    expectBytes(stream, "3 0 0.0\n"); // ABORT_PROCESS

    CommandStruct captured{};
    EXPECT_CALL(queue, try_push(::testing::_))
        .WillOnce(
            [&](const CommandStruct &cmd)
            {
                captured = cmd;
                return true;
            });

    task.tick(SputterMicros(0));
    EXPECT_EQ(captured.id, CmdID::ABORT_PROCESS);
}

// ---------------------------------------------------------------------------
// Queue full â€” command silently dropped, no crash
// ---------------------------------------------------------------------------

TEST_F(CommsTaskTest, QueueFull_CommandDropped_NoCrash)
{
    expectBytes(stream, "1 0 30.0\n");
    ON_CALL(queue, try_push(::testing::_)).WillByDefault(Return(false));
    EXPECT_NO_FATAL_FAILURE(task.tick(SputterMicros(0)));
}

// ---------------------------------------------------------------------------
// Partial command across two ticks â€” accumulated correctly
// ---------------------------------------------------------------------------

TEST_F(CommsTaskTest, PartialCommand_AccumulatesAcrossTicks)
{
    // First tick: only the prefix, no newline
    {
        const char       *part = "1 0 ";
        const std::size_t len  = std::strlen(part);
        ON_CALL(stream, available()).WillByDefault(Return(len));
        ON_CALL(stream, read(::testing::_, ::testing::_))
            .WillByDefault(DoAll(SetArrayArgument<0>(reinterpret_cast<const uint8_t *>(part),
                                                     reinterpret_cast<const uint8_t *>(part) + len),
                                 Return(len)));
    }
    EXPECT_CALL(queue, try_push(::testing::_)).Times(0);
    task.tick(SputterMicros(0));

    // Second tick: the rest with newline
    expectBytes(stream, "40.0\n");
    EXPECT_CALL(queue, try_push(::testing::_)).Times(1);
    task.tick(SputterMicros(1));
}

// ---------------------------------------------------------------------------
// NACK wire format — queue full response includes cmd_id, sub_id, value
// ---------------------------------------------------------------------------

TEST_F(CommsTaskTest, QueueFull_NackWireFormat_ContainsAllFields)
{
    // "1 0 50.0\n" → CmdID::SET_GAS_FLOW (1), targetDevice=0, value=50.0
    expectBytes(stream, "1 0 50.0\n");
    ON_CALL(queue, try_push(::testing::_)).WillByDefault(Return(false));

    std::string captured;
    ON_CALL(stream, write(::testing::_, ::testing::_))
        .WillByDefault(
            [&](const uint8_t *data, std::size_t len)
            {
                captured.append(reinterpret_cast<const char *>(data), len);
                return len;
            });

    task.tick(SputterMicros(0));

    // Wire format: "NACK <cmd_id> <sub_id> <value>\n"
    EXPECT_NE(captured.find("NACK"), std::string::npos) << "Response must begin with NACK";
    EXPECT_NE(captured.find(" 1 "), std::string::npos) << "cmd_id (1) must be present";
    EXPECT_NE(captured.find(" 0 "), std::string::npos) << "sub_id (0) must be present";
    EXPECT_NE(captured.find("50."), std::string::npos) << "value (50.0) must be present";
}

// ---------------------------------------------------------------------------
// Invalid line — no push
// ---------------------------------------------------------------------------

TEST_F(CommsTaskTest, InvalidLine_NotPushedToQueue)
{
    expectBytes(stream, "INVALID_COMMAND\n");
    EXPECT_CALL(queue, try_push(::testing::_)).Times(0);
    task.tick(SputterMicros(0));
}
