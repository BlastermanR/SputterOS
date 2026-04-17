/**
 * @file test_CLI.cpp
 * @brief Unit tests for CLI<Cfg>.
 *
 * Feeds synthetic bytes through MockStreamReader to verify command parsing,
 * hasCommand/getCommand flow, and transmit helpers (print/println/flush).
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */

#include "MockStreamReader.h"
#include "TestConfig.h"
#include "sputteros/comms/CLI.h"
#include <cstring>
#include <gtest/gtest.h>

using namespace SputterOS;
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

static void feedStream(NiceMock<MockStreamReader> &stream, const char *data)
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

class CLITest : public ::testing::Test
{
  protected:
    NiceMock<MockStreamReader> stream;
    CLI<Cfg>                   cli{&stream};

    void SetUp() override { ON_CALL(stream, available()).WillByDefault(Return(0)); }
};

// ---------------------------------------------------------------------------
// Null stream — tick() must not crash
// ---------------------------------------------------------------------------

TEST_F(CLITest, NullStream_TickDoesNotCrash)
{
    CLI<Cfg> nullCli(nullptr);
    EXPECT_NO_FATAL_FAILURE(nullCli.tick());
}

// ---------------------------------------------------------------------------
// hasCommand before any tick
// ---------------------------------------------------------------------------

TEST_F(CLITest, HasCommand_BeforeAnyTick_ReturnsFalse) { EXPECT_FALSE(cli.hasCommand()); }

// ---------------------------------------------------------------------------
// No bytes available — no command
// ---------------------------------------------------------------------------

TEST_F(CLITest, EmptyStream_NoCommand)
{
    cli.tick();
    EXPECT_FALSE(cli.hasCommand());
}

// ---------------------------------------------------------------------------
// Valid complete command after tick
// ---------------------------------------------------------------------------

TEST_F(CLITest, ValidCommand_HasCommandAfterTick)
{
    feedStream(stream, "1 0 50.0\n");
    cli.tick();
    EXPECT_TRUE(cli.hasCommand());
}

TEST_F(CLITest, ValidCommand_GetCommandReturnsCorrectID)
{
    feedStream(stream, "3 0 0.0\n"); // ABORT_PROCESS
    cli.tick();

    CommandStruct cmd{};
    ASSERT_TRUE(cli.getCommand(cmd));
    EXPECT_EQ(cmd.id, CmdID::ABORT_PROCESS);
}

TEST_F(CLITest, GetCommand_ClearsPendingFlag)
{
    feedStream(stream, "1 0 10.0\n");
    cli.tick();
    CommandStruct cmd{};
    cli.getCommand(cmd);
    EXPECT_FALSE(cli.hasCommand());
}

// ---------------------------------------------------------------------------
// Invalid command — no pending command
// ---------------------------------------------------------------------------

TEST_F(CLITest, InvalidCommand_HasCommandIsFalse)
{
    feedStream(stream, "BADCMD\n");
    cli.tick();
    EXPECT_FALSE(cli.hasCommand());
}

// ---------------------------------------------------------------------------
// Partial command across ticks accumulates correctly
// ---------------------------------------------------------------------------

TEST_F(CLITest, PartialCommand_AccumulatesAcrossTicks)
{
    feedStream(stream, "2 1 ");
    cli.tick();
    EXPECT_FALSE(cli.hasCommand());

    feedStream(stream, "75.0\n");
    cli.tick();
    EXPECT_TRUE(cli.hasCommand());

    CommandStruct cmd{};
    ASSERT_TRUE(cli.getCommand(cmd));
    EXPECT_EQ(cmd.id, CmdID::SET_POWER_WATTAGE);
    EXPECT_NEAR(cmd.value, 75.0f, 0.01f);
}

// ---------------------------------------------------------------------------
// print / println write to stream
// ---------------------------------------------------------------------------

TEST_F(CLITest, Print_WritesToStream)
{
    const char *msg = "hello";
    EXPECT_CALL(stream, write(::testing::_, 5u)).Times(1);
    cli.print(msg);
}

TEST_F(CLITest, Println_WritesStringAndNewline)
{
    // println writes the string then a single '\n' byte
    EXPECT_CALL(stream, write(::testing::_, ::testing::_)).Times(2);
    cli.println("hi");
}

TEST_F(CLITest, Print_NullString_DoesNotCrash) { EXPECT_NO_FATAL_FAILURE(cli.print(nullptr)); }

// ---------------------------------------------------------------------------
// builder + flush
// ---------------------------------------------------------------------------

TEST_F(CLITest, Flush_EmptyBuilder_NoWrite)
{
    EXPECT_CALL(stream, write(::testing::_, ::testing::_)).Times(0);
    cli.flush();
}

TEST_F(CLITest, Flush_WritesBuilderContentsAndClearsBuilder)
{
    cli.builder().append("TEST");
    EXPECT_CALL(stream, write(::testing::_, 4u)).Times(1);
    cli.flush();
    EXPECT_EQ(cli.builder().length(), 0u);
}
