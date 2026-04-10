/**
 * @file test_CommandParser.cpp
 * @brief Unit tests for CommandParser<Cfg>.
 *
 * Feeds raw byte sequences through `feedByte()` and validates `getCommand()`
 * output to ensure the ASCII parsing protocol is correct under all inputs.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */

#include "TestConfig.h"
#include "sputteros/logic/CommandParser.h"
#include <cstring>
#include <gtest/gtest.h>

using namespace SputterOS;
using Cfg           = TestConfig;
using CmdID         = Cfg::CmdID;
using CommandStruct = Cfg::Command;

// ---------------------------------------------------------------------------
// Helper: feed a null-terminated string into the parser byte-by-byte.
// Returns true if the LAST byte caused a successful parse.
// ---------------------------------------------------------------------------
static bool feedString(CommandParser<Cfg> &p, const char *s)
{
    bool result = false;
    while (*s)
    {
        result = p.feedByte(static_cast<uint8_t>(*s++));
    }
    return result;
}

// ===========================================================================
// Fixture
// ===========================================================================

class CommandParserTest : public ::testing::Test
{
  protected:
    CommandParser<Cfg> parser;
};

// ---------------------------------------------------------------------------
// Valid parse — format: "<cmdId> <device> <value>\n"
// ---------------------------------------------------------------------------

TEST_F(CommandParserTest, ValidCommand_SetGasFlow_ParsesAllFields)
{
    // "1 0 50.0\n" => SET_GAS_FLOW, device 0, value 50.0
    bool complete = feedString(parser, "1 0 50.0\n");
    EXPECT_TRUE(complete);

    CommandStruct cmd{};
    EXPECT_TRUE(parser.getCommand(cmd));
    EXPECT_EQ(cmd.id, CmdID::SET_GAS_FLOW);
    EXPECT_EQ(cmd.targetDevice, 0u);
    EXPECT_NEAR(cmd.value, 50.0f, 0.001f);
}

TEST_F(CommandParserTest, ValidCommand_AbortProcess_ParsesCorrectly)
{
    // CmdID::ABORT_PROCESS is the last enum value (3)
    feedString(parser, "3 0 0.0\n");
    CommandStruct cmd{};
    ASSERT_TRUE(parser.getCommand(cmd));
    EXPECT_EQ(cmd.id, CmdID::ABORT_PROCESS);
}

TEST_F(CommandParserTest, ValidCommand_SetState_DeviceAndValuePreserved)
{
    feedString(parser, "0 2 1.0\n");
    CommandStruct cmd{};
    ASSERT_TRUE(parser.getCommand(cmd));
    EXPECT_EQ(cmd.id, CmdID::SET_STATE);
    EXPECT_EQ(cmd.targetDevice, 2u);
    EXPECT_NEAR(cmd.value, 1.0f, 0.001f);
}

TEST_F(CommandParserTest, ValidCommand_NegativeValue_AcceptedByParser)
{
    feedString(parser, "2 0 -100.0\n");
    CommandStruct cmd{};
    ASSERT_TRUE(parser.getCommand(cmd));
    EXPECT_NEAR(cmd.value, -100.0f, 0.001f);
}

// ---------------------------------------------------------------------------
// Partial input — no command available until newline
// ---------------------------------------------------------------------------

TEST_F(CommandParserTest, PartialInput_NoCommandBeforeNewline)
{
    feedString(parser, "1 0 50");
    CommandStruct cmd{};
    EXPECT_FALSE(parser.getCommand(cmd));
}

TEST_F(CommandParserTest, PartialInput_CommandAvailableAfterNewline)
{
    feedString(parser, "1 0 50");
    {
        CommandStruct tmp{};
        EXPECT_FALSE(parser.getCommand(tmp));
    }

    parser.feedByte('\n');
    CommandStruct cmd{};
    EXPECT_TRUE(parser.getCommand(cmd));
    EXPECT_EQ(cmd.id, CmdID::SET_GAS_FLOW);
}

// ---------------------------------------------------------------------------
// CR-LF line ending — both '\r' and '\n' trigger a parse attempt
// ---------------------------------------------------------------------------

TEST_F(CommandParserTest, CarriageReturn_TriggersParse)
{
    feedString(parser, "1 0 25.0\r");
    CommandStruct cmd{};
    EXPECT_TRUE(parser.getCommand(cmd));
    EXPECT_NEAR(cmd.value, 25.0f, 0.001f);
}

// ---------------------------------------------------------------------------
// Buffer overflow — line longer than kMaxLineLen (64) resets the parser
// ---------------------------------------------------------------------------

TEST_F(CommandParserTest, Overflow_LongLineIsDiscarded)
{
    // 65 characters before the field separator — exceeds kMaxLineLen(64)
    for (int i = 0; i < 65; ++i)
    {
        parser.feedByte('A');
    }
    parser.feedByte('\n');
    CommandStruct cmd{};
    EXPECT_FALSE(parser.getCommand(cmd));
}

TEST_F(CommandParserTest, Overflow_ParserRecoversSendingValidCommandAfterwards)
{
    // Spam the buffer to overflow it, then send a valid command
    for (int i = 0; i < 65; ++i)
    {
        parser.feedByte('X');
    }
    parser.feedByte('\n');
    feedString(parser, "2 1 75.5\n");
    CommandStruct cmd{};
    ASSERT_TRUE(parser.getCommand(cmd));
    EXPECT_EQ(cmd.id, CmdID::SET_POWER_WATTAGE);
    EXPECT_NEAR(cmd.value, 75.5f, 0.01f);
}

// ---------------------------------------------------------------------------
// Invalid content — out-of-range CmdID is rejected
// ---------------------------------------------------------------------------

TEST_F(CommandParserTest, InvalidCommandID_TooLarge_IsRejected)
{
    // 99 is well beyond the range of the CmdID enum
    feedString(parser, "99 0 0.0\n");
    CommandStruct cmd{};
    EXPECT_FALSE(parser.getCommand(cmd));
}

TEST_F(CommandParserTest, InvalidCommandID_Negative_IsRejected)
{
    feedString(parser, "-1 0 0.0\n");
    CommandStruct cmd{};
    EXPECT_FALSE(parser.getCommand(cmd));
}

TEST_F(CommandParserTest, InvalidContent_AlphaInCommandIDField_IsRejected)
{
    feedString(parser, "SET 0 0.0\n");
    CommandStruct cmd{};
    EXPECT_FALSE(parser.getCommand(cmd));
}

TEST_F(CommandParserTest, InvalidContent_MissingValueField_IsRejected)
{
    feedString(parser, "1 0\n");
    CommandStruct cmd{};
    EXPECT_FALSE(parser.getCommand(cmd));
}

// ---------------------------------------------------------------------------
// Multiple consecutive commands in sequence
// ---------------------------------------------------------------------------

TEST_F(CommandParserTest, MultipleCommands_ParseInOrder)
{
    feedString(parser, "0 0 1.0\n");
    CommandStruct cmd1{};
    ASSERT_TRUE(parser.getCommand(cmd1));
    EXPECT_EQ(cmd1.id, CmdID::SET_STATE);

    feedString(parser, "1 0 30.0\n");
    CommandStruct cmd2{};
    ASSERT_TRUE(parser.getCommand(cmd2));
    EXPECT_EQ(cmd2.id, CmdID::SET_GAS_FLOW);
}

// ---------------------------------------------------------------------------
// reset() clears partial state
// ---------------------------------------------------------------------------

TEST_F(CommandParserTest, Reset_DiscardPartialCommand)
{
    feedString(parser, "1 0 50");
    parser.reset();
    parser.feedByte('\n'); // The lone newline should not produce a command
    CommandStruct cmd{};
    EXPECT_FALSE(parser.getCommand(cmd));
}

TEST_F(CommandParserTest, Reset_ClearsPendingCommand)
{
    feedString(parser, "1 0 50.0\n");
    // A command is now pending; reset before consuming it
    parser.reset();
    CommandStruct cmd{};
    // reset() must clear m_hasPending
    EXPECT_FALSE(parser.getCommand(cmd));
}

// ---------------------------------------------------------------------------
// getCommand is one-shot — second call returns false
// ---------------------------------------------------------------------------

TEST_F(CommandParserTest, GetCommand_ClearsPendingFlag)
{
    feedString(parser, "1 0 10.0\n");
    CommandStruct cmd{};
    ASSERT_TRUE(parser.getCommand(cmd));
    EXPECT_FALSE(parser.getCommand(cmd)); // Second call must return false
}

// ---------------------------------------------------------------------------
// Empty line (lone newline) is treated as a no-op
// ---------------------------------------------------------------------------

TEST_F(CommandParserTest, EmptyLine_ProducesNoCommand)
{
    parser.feedByte('\n');
    CommandStruct cmd{};
    EXPECT_FALSE(parser.getCommand(cmd));
}
