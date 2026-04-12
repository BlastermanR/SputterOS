/**
 * @file test_DualModeCLI.cpp
 * @brief Unit tests for CLI<Cfg> dual-mode (TEXT + FRAMED) behavior.
 *
 * Tests backward-compatible TEXT mode, handshake detection triggering
 * FRAMED mode switch, framed command processing, framed ACK/NACK output,
 * exit handshake reverting to TEXT, and all framed output helpers.
 *
 * Uses a FakeStream that allows scripted byte injection and output capture.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/12/2026
 */

#include "TestConfig.h"
#include "sputteros/comms/CLI.h"
#include "sputteros/comms/protocol/CobsCodec.h"
#include "sputteros/comms/protocol/CommsMode.h"
#include "sputteros/comms/protocol/Crc16.h"
#include "sputteros/comms/protocol/FrameConstants.h"
#include "sputteros/comms/protocol/FrameEncoder.h"
#include "sputteros/comms/protocol/IProtocolHandler.h"
#include "sputteros/comms/protocol/MessageType.h"
#include "sputteros/comms/protocol/ResponseSerializer.h"
#include <algorithm>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace SputterOS;
using Cfg           = TestConfig;
using CmdID         = Cfg::CmdID;
using CommandStruct = Cfg::Command;

// ===========================================================================
// FakeStream — scripted byte injection + output capture
// ===========================================================================

class FakeStream : public IStream
{
  public:
    /** @brief Queue bytes for the CLI to read. */
    void inject(const uint8_t *data, std::size_t len) { m_rx.insert(m_rx.end(), data, data + len); }
    void inject(const char *str) { inject(reinterpret_cast<const uint8_t *>(str), std::strlen(str)); }

    /** @brief Get all bytes written by the CLI. */
    const std::vector<uint8_t> &captured() const { return m_tx; }
    std::string capturedString() const { return std::string(m_tx.begin(), m_tx.end()); }
    void clearCaptured() { m_tx.clear(); }

    std::size_t available() const override { return m_rx.size(); }

    std::size_t read(uint8_t *buffer, std::size_t max_len) override
    {
        std::size_t n = std::min(max_len, m_rx.size());
        std::memcpy(buffer, m_rx.data(), n);
        m_rx.erase(m_rx.begin(), m_rx.begin() + static_cast<std::ptrdiff_t>(n));
        return n;
    }

    std::size_t write(const uint8_t *data, std::size_t len) override
    {
        m_tx.insert(m_tx.end(), data, data + len);
        return len;
    }

    bool isConnected() const override { return true; }

  private:
    std::vector<uint8_t> m_rx; // Bytes to be read by CLI
    std::vector<uint8_t> m_tx; // Bytes written by CLI
};

// ===========================================================================
// Stub Protocol Handler (captures callbacks)
// ===========================================================================

class StubHandler : public IProtocolHandler<Cfg>
{
  public:
    StubHandler() = default;

    bool     commandCalled    = false;
    bool     handshakeCalled  = false;
    bool     exitCalled       = false;
    bool     metricsCalled    = false;
    bool     heartbeatCalled  = false;
    uint16_t lastVersion      = 0;
    uint8_t  lastSeq          = 0;

    void onCommand(const CommandStruct &, uint8_t seqNum) override
    {
        commandCalled = true;
        lastSeq       = seqNum;
    }

    void onHandshakeRequest(uint16_t version, uint8_t seqNum) override
    {
        handshakeCalled = true;
        lastVersion     = version;
        lastSeq         = seqNum;
    }

    void onExitHandshake(uint8_t seqNum) override
    {
        exitCalled = true;
        lastSeq    = seqNum;
    }

    void onMetricsRequest(uint8_t seqNum) override
    {
        metricsCalled = true;
        lastSeq       = seqNum;
    }

    void onHeartbeat(uint8_t seqNum) override
    {
        heartbeatCalled = true;
        lastSeq         = seqNum;
    }
};

// ===========================================================================
// Frame building helpers (for test input)
// ===========================================================================

/**
 * @brief Build a complete COBS wire frame for injection into FakeStream.
 */
static std::vector<uint8_t> buildWireFrame(MessageType type, uint8_t seqNum,
                                            const uint8_t *payload, std::size_t payloadLen)
{
    FrameEncoder<256> encoder;
    uint8_t wire[FrameEncoder<256>::kMaxWireSize];
    std::size_t n = encoder.encode(type, seqNum, payload, payloadLen, wire, sizeof(wire));
    return std::vector<uint8_t>(wire, wire + n);
}

static std::vector<uint8_t> buildHandshakeFrame(uint8_t seqNum = 1)
{
    uint8_t payload[6];
    payload[0] = static_cast<uint8_t>(kProtocolVersion & 0xFF);
    payload[1] = static_cast<uint8_t>((kProtocolVersion >> 8) & 0xFF);
    payload[2] = static_cast<uint8_t>(kHandshakeMagic & 0xFF);
    payload[3] = static_cast<uint8_t>((kHandshakeMagic >> 8) & 0xFF);
    payload[4] = static_cast<uint8_t>((kHandshakeMagic >> 16) & 0xFF);
    payload[5] = static_cast<uint8_t>((kHandshakeMagic >> 24) & 0xFF);
    auto frame = buildWireFrame(MessageType::HANDSHAKE_REQ, seqNum, payload, 6);
    // Prepend a 0x00 sync byte — required for handshake detection in TEXT mode.
    frame.insert(frame.begin(), 0x00);
    return frame;
}

static std::vector<uint8_t> buildCommandFrame(uint8_t cmdId, uint8_t device, float value,
                                               uint8_t seqNum = 2)
{
    uint8_t payload[6];
    ResponseSerializer::serializeCommand(cmdId, device, value, payload, sizeof(payload));
    return buildWireFrame(MessageType::COMMAND, seqNum, payload, 6);
}

static std::vector<uint8_t> buildExitFrame(uint8_t seqNum = 10)
{
    return buildWireFrame(MessageType::EXIT_HANDSHAKE, seqNum, nullptr, 0);
}

// ===========================================================================
// Fixture
// ===========================================================================

class DualModeCLITest : public ::testing::Test
{
  protected:
    FakeStream      stream;
    StubHandler     handler;
    CLI<Cfg>        cli{&stream};

    void SetUp() override { cli.setProtocolHandler(&handler); }

    /** @brief Pump the CLI until the stream is empty. */
    void drainAll()
    {
        int guard = 100;
        while (stream.available() > 0 && --guard > 0)
        {
            cli.tick();
        }
    }
};

// ===========================================================================
// TEXT mode — backward compatibility
// ===========================================================================

TEST_F(DualModeCLITest, DefaultMode_IsText)
{
    EXPECT_EQ(cli.getMode(), CommsMode::TEXT);
}

TEST_F(DualModeCLITest, TextMode_ValidCommand_Parseable)
{
    stream.inject("1 0 50.0\n"); // CmdID::SET_GAS_FLOW
    drainAll();

    EXPECT_TRUE(cli.hasCommand());
    CommandStruct cmd{};
    EXPECT_TRUE(cli.getCommand(cmd));
    EXPECT_EQ(cmd.id, CmdID::SET_GAS_FLOW);
    EXPECT_EQ(cmd.targetDevice, 0);
    EXPECT_FLOAT_EQ(cmd.value, 50.0f);
}

TEST_F(DualModeCLITest, TextMode_InvalidLine_NoCommand)
{
    stream.inject("GARBAGE\n");
    drainAll();
    EXPECT_FALSE(cli.hasCommand());
}

TEST_F(DualModeCLITest, TextMode_Print_WritesToStream)
{
    cli.print("hello");
    EXPECT_EQ(stream.capturedString(), "hello");
}

TEST_F(DualModeCLITest, TextMode_Println_AppendsNewline)
{
    cli.println("test");
    EXPECT_EQ(stream.capturedString(), "test\n");
}

TEST_F(DualModeCLITest, TextMode_BuilderFlush_WritesToStream)
{
    cli.builder().clear();
    cli.builder().append("STATE:1\n");
    cli.flush();
    EXPECT_EQ(stream.capturedString(), "STATE:1\n");
}

// ===========================================================================
// Handshake detection — TEXT → FRAMED
// ===========================================================================

TEST_F(DualModeCLITest, HandshakeProbe_SwitchesToFramedMode)
{
    auto frame = buildHandshakeFrame(1);
    stream.inject(frame.data(), frame.size());
    drainAll();

    EXPECT_EQ(cli.getMode(), CommsMode::FRAMED);
    EXPECT_TRUE(handler.handshakeCalled);
}

TEST_F(DualModeCLITest, HandshakeProbe_BadMagic_StaysText)
{
    // Build a malformed handshake (wrong magic bytes)
    uint8_t payload[6] = {1, 0, 0xDE, 0xAD, 0xBE, 0xEF};
    auto frame = buildWireFrame(MessageType::HANDSHAKE_REQ, 1, payload, 6);
    stream.inject(frame.data(), frame.size());
    drainAll();

    EXPECT_EQ(cli.getMode(), CommsMode::TEXT);
    EXPECT_FALSE(handler.handshakeCalled);
}

TEST_F(DualModeCLITest, HandshakeProbe_NoHandler_StaysText)
{
    CLI<Cfg> bareCliObj(&stream);
    // No setProtocolHandler() called

    auto frame = buildHandshakeFrame(1);
    stream.inject(frame.data(), frame.size());

    int guard = 100;
    while (stream.available() > 0 && --guard > 0)
    {
        bareCliObj.tick();
    }

    EXPECT_EQ(bareCliObj.getMode(), CommsMode::TEXT);
}

// ===========================================================================
// FRAMED mode — command processing
// ===========================================================================

TEST_F(DualModeCLITest, FramedMode_CommandFrame_ProducesCommand)
{
    // Switch to framed mode first
    auto hs = buildHandshakeFrame(1);
    stream.inject(hs.data(), hs.size());
    drainAll();
    ASSERT_EQ(cli.getMode(), CommsMode::FRAMED);

    stream.clearCaptured();

    // Now send a COMMAND frame
    float val = 75.0f;
    auto cmd  = buildCommandFrame(static_cast<uint8_t>(CmdID::SET_POWER_WATTAGE), 2, val, 5);
    stream.inject(cmd.data(), cmd.size());
    drainAll();

    EXPECT_TRUE(cli.hasCommand());
    CommandStruct parsed{};
    EXPECT_TRUE(cli.getCommand(parsed));
    EXPECT_EQ(parsed.id, CmdID::SET_POWER_WATTAGE);
    EXPECT_EQ(parsed.targetDevice, 2);
    EXPECT_FLOAT_EQ(parsed.value, 75.0f);
    EXPECT_EQ(cli.getLastFramedSeqNum(), 5);
}

TEST_F(DualModeCLITest, FramedMode_NoCommand_HasCommandFalse)
{
    cli.enterFramedMode();
    cli.tick(); // Empty stream
    EXPECT_FALSE(cli.hasCommand());
}

// ===========================================================================
// Exit handshake — FRAMED → TEXT
// ===========================================================================

TEST_F(DualModeCLITest, ExitFramedMode_SwitchesBackToText)
{
    cli.enterFramedMode();
    ASSERT_EQ(cli.getMode(), CommsMode::FRAMED);

    cli.exitFramedMode();
    EXPECT_EQ(cli.getMode(), CommsMode::TEXT);
}

TEST_F(DualModeCLITest, ExitHandshake_Frame_RevertsToText)
{
    // Enter framed mode via handshake
    auto hs = buildHandshakeFrame(1);
    stream.inject(hs.data(), hs.size());
    drainAll();
    ASSERT_EQ(cli.getMode(), CommsMode::FRAMED);

    // Send EXIT_HANDSHAKE — the handler's onExitHandshake is called,
    // but it's the CommsTask that calls exitFramedMode(). Since we're
    // testing CLI alone, we manually verify dispatch.
    auto exitFrame = buildExitFrame(10);
    stream.inject(exitFrame.data(), exitFrame.size());
    drainAll();

    EXPECT_TRUE(handler.exitCalled);
    EXPECT_EQ(handler.lastSeq, 10);
}

TEST_F(DualModeCLITest, AfterExitFramedMode_TextCommandsWork)
{
    cli.enterFramedMode();
    cli.exitFramedMode();
    ASSERT_EQ(cli.getMode(), CommsMode::TEXT);

    stream.inject("3 0 0.0\n"); // ABORT_PROCESS
    drainAll();

    EXPECT_TRUE(cli.hasCommand());
    CommandStruct cmd{};
    EXPECT_TRUE(cli.getCommand(cmd));
    EXPECT_EQ(cmd.id, CmdID::ABORT_PROCESS);
}

// ===========================================================================
// Framed output helpers
// ===========================================================================

TEST_F(DualModeCLITest, SendFramedAck_ProducesOutput)
{
    cli.sendFramedAck(7, 0x01);
    EXPECT_FALSE(stream.captured().empty());
    // Verify the output ends with a 0x00 delimiter (COBS frame)
    EXPECT_EQ(stream.captured().back(), 0x00);
}

TEST_F(DualModeCLITest, SendFramedNack_ProducesOutput)
{
    cli.sendFramedNack(3, 0x02, 1, 50.0f);
    EXPECT_FALSE(stream.captured().empty());
    EXPECT_EQ(stream.captured().back(), 0x00);
}

TEST_F(DualModeCLITest, SendFramedHandshakeResp_ProducesOutput)
{
    cli.sendFramedHandshakeResp(1);
    EXPECT_FALSE(stream.captured().empty());
    EXPECT_EQ(stream.captured().back(), 0x00);
}

TEST_F(DualModeCLITest, SendFramedHeartbeat_ProducesOutput)
{
    cli.sendFramedHeartbeat(99);
    EXPECT_FALSE(stream.captured().empty());
    EXPECT_EQ(stream.captured().back(), 0x00);
}

TEST_F(DualModeCLITest, SendFramedData_ProducesOutput)
{
    const uint8_t data[] = {0xCA, 0xFE};
    cli.sendFramedData(data, 2);
    EXPECT_FALSE(stream.captured().empty());
    EXPECT_EQ(stream.captured().back(), 0x00);
}

TEST_F(DualModeCLITest, SendFramedLog_ProducesOutput)
{
    cli.sendFramedLog(1, "test message");
    EXPECT_FALSE(stream.captured().empty());
    EXPECT_EQ(stream.captured().back(), 0x00);
}

TEST_F(DualModeCLITest, SendFramedMetricsResp_EmptyPayload_ProducesOutput)
{
    cli.sendFramedMetricsResp(5, nullptr, 0);
    EXPECT_FALSE(stream.captured().empty());
    EXPECT_EQ(stream.captured().back(), 0x00);
}

// ===========================================================================
// Null stream safety
// ===========================================================================

TEST_F(DualModeCLITest, NullStream_TickDoesNotCrash)
{
    CLI<Cfg> nullCli(nullptr);
    EXPECT_NO_FATAL_FAILURE(nullCli.tick());
    EXPECT_FALSE(nullCli.hasCommand());
}

TEST_F(DualModeCLITest, NullStream_SendDoesNotCrash)
{
    CLI<Cfg> nullCli(nullptr);
    EXPECT_NO_FATAL_FAILURE(nullCli.sendFramedAck(0, 0));
    EXPECT_NO_FATAL_FAILURE(nullCli.print("test"));
    EXPECT_NO_FATAL_FAILURE(nullCli.flush());
}

TEST_F(DualModeCLITest, HasStream_WithStream_ReturnsTrue)
{
    EXPECT_TRUE(cli.hasStream());
}

TEST_F(DualModeCLITest, HasStream_Null_ReturnsFalse)
{
    CLI<Cfg> nullCli(nullptr);
    EXPECT_FALSE(nullCli.hasStream());
}
