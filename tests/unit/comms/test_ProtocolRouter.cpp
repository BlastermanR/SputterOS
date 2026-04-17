/**
 * @file test_ProtocolRouter.cpp
 * @brief Unit tests for ProtocolRouter<Cfg>.
 *
 * Validates message type dispatch, payload deserialization, and error
 * handling for unknown types and malformed payloads.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/12/2026
 */

#include "TestConfig.h"
#include "sputteros/comms/protocol/FrameConstants.h"
#include "sputteros/comms/protocol/IProtocolHandler.h"
#include "sputteros/comms/protocol/MessageType.h"
#include "sputteros/comms/protocol/ProtocolRouter.h"
#include "sputteros/comms/protocol/ResponseSerializer.h"
#include <cstring>
#include <gtest/gtest.h>

using namespace SputterOS;
using Cfg           = TestConfig;
using CmdID         = Cfg::CmdID;
using CommandStruct = Cfg::Command;

// ===========================================================================
// Stub Protocol Handler
// ===========================================================================

/**
 * @brief Captures dispatched callbacks for test verification.
 */
class StubProtocolHandler : public IProtocolHandler<Cfg>
{
  public:
    StubProtocolHandler() = default;

    // Captured state
    bool          commandCalled = false;
    CommandStruct lastCommand{};
    uint8_t       lastCommandSeq = 0;

    bool     handshakeCalled  = false;
    uint16_t lastVersion      = 0;
    uint8_t  lastHandshakeSeq = 0;

    bool    exitCalled  = false;
    uint8_t lastExitSeq = 0;

    bool    metricsCalled  = false;
    uint8_t lastMetricsSeq = 0;

    bool    heartbeatCalled  = false;
    uint8_t lastHeartbeatSeq = 0;

    void onCommand(const CommandStruct &cmd, uint8_t seqNum) override
    {
        commandCalled  = true;
        lastCommand    = cmd;
        lastCommandSeq = seqNum;
    }

    void onHandshakeRequest(uint16_t version, uint8_t seqNum) override
    {
        handshakeCalled  = true;
        lastVersion      = version;
        lastHandshakeSeq = seqNum;
    }

    void onExitHandshake(uint8_t seqNum) override
    {
        exitCalled  = true;
        lastExitSeq = seqNum;
    }

    void onMetricsRequest(uint8_t seqNum) override
    {
        metricsCalled  = true;
        lastMetricsSeq = seqNum;
    }

    void onHeartbeat(uint8_t seqNum) override
    {
        heartbeatCalled  = true;
        lastHeartbeatSeq = seqNum;
    }
};

// ===========================================================================
// Fixture
// ===========================================================================

class ProtocolRouterTest : public ::testing::Test
{
  protected:
    StubProtocolHandler handler;
    ProtocolRouter<Cfg> router{&handler};
};

// ===========================================================================
// COMMAND dispatch
// ===========================================================================

TEST_F(ProtocolRouterTest, DispatchCommand_ValidPayload_InvokesOnCommand)
{
    uint8_t payload[6];
    float   val = 42.5f;
    payload[0]  = static_cast<uint8_t>(CmdID::SET_GAS_FLOW);
    payload[1]  = 3;
    std::memcpy(&payload[2], &val, sizeof(float));

    EXPECT_TRUE(router.dispatch(MessageType::COMMAND, 7, payload, 6));
    EXPECT_TRUE(handler.commandCalled);
    EXPECT_EQ(handler.lastCommand.id, CmdID::SET_GAS_FLOW);
    EXPECT_EQ(handler.lastCommand.targetDevice, 3);
    EXPECT_FLOAT_EQ(handler.lastCommand.value, 42.5f);
    EXPECT_EQ(handler.lastCommandSeq, 7);
}

TEST_F(ProtocolRouterTest, DispatchCommand_WrongPayloadLen_ReturnsFalse)
{
    uint8_t payload[3] = {0, 1, 2};
    EXPECT_FALSE(router.dispatch(MessageType::COMMAND, 1, payload, 3));
    EXPECT_FALSE(handler.commandCalled);
}

TEST_F(ProtocolRouterTest, DispatchCommand_TooLong_ReturnsFalse)
{
    uint8_t payload[8] = {};
    EXPECT_FALSE(router.dispatch(MessageType::COMMAND, 1, payload, 8));
    EXPECT_FALSE(handler.commandCalled);
}

// ===========================================================================
// HANDSHAKE_REQ dispatch
// ===========================================================================

TEST_F(ProtocolRouterTest, DispatchHandshake_ValidPayload_InvokesOnHandshakeRequest)
{
    uint8_t payload[6];
    // version = kProtocolVersion (1), magic = kHandshakeMagic ("SPOS")
    payload[0] = static_cast<uint8_t>(kProtocolVersion & 0xFF);
    payload[1] = static_cast<uint8_t>((kProtocolVersion >> 8) & 0xFF);
    payload[2] = static_cast<uint8_t>(kHandshakeMagic & 0xFF);
    payload[3] = static_cast<uint8_t>((kHandshakeMagic >> 8) & 0xFF);
    payload[4] = static_cast<uint8_t>((kHandshakeMagic >> 16) & 0xFF);
    payload[5] = static_cast<uint8_t>((kHandshakeMagic >> 24) & 0xFF);

    EXPECT_TRUE(router.dispatch(MessageType::HANDSHAKE_REQ, 99, payload, 6));
    EXPECT_TRUE(handler.handshakeCalled);
    EXPECT_EQ(handler.lastVersion, kProtocolVersion);
    EXPECT_EQ(handler.lastHandshakeSeq, 99);
}

TEST_F(ProtocolRouterTest, DispatchHandshake_BadMagic_ReturnsFalse)
{
    uint8_t payload[6] = {1, 0, 0xDE, 0xAD, 0xBE, 0xEF};
    EXPECT_FALSE(router.dispatch(MessageType::HANDSHAKE_REQ, 1, payload, 6));
    EXPECT_FALSE(handler.handshakeCalled);
}

TEST_F(ProtocolRouterTest, DispatchHandshake_ShortPayload_ReturnsFalse)
{
    uint8_t payload[4] = {};
    EXPECT_FALSE(router.dispatch(MessageType::HANDSHAKE_REQ, 1, payload, 4));
    EXPECT_FALSE(handler.handshakeCalled);
}

// ===========================================================================
// EXIT_HANDSHAKE dispatch
// ===========================================================================

TEST_F(ProtocolRouterTest, DispatchExitHandshake_Dispatches)
{
    EXPECT_TRUE(router.dispatch(MessageType::EXIT_HANDSHAKE, 42, nullptr, 0));
    EXPECT_TRUE(handler.exitCalled);
    EXPECT_EQ(handler.lastExitSeq, 42);
}

// ===========================================================================
// METRICS_REQ dispatch
// ===========================================================================

TEST_F(ProtocolRouterTest, DispatchMetricsReq_Dispatches)
{
    EXPECT_TRUE(router.dispatch(MessageType::METRICS_REQ, 10, nullptr, 0));
    EXPECT_TRUE(handler.metricsCalled);
    EXPECT_EQ(handler.lastMetricsSeq, 10);
}

// ===========================================================================
// HEARTBEAT dispatch
// ===========================================================================

TEST_F(ProtocolRouterTest, DispatchHeartbeat_Dispatches)
{
    EXPECT_TRUE(router.dispatch(MessageType::HEARTBEAT, 55, nullptr, 0));
    EXPECT_TRUE(handler.heartbeatCalled);
    EXPECT_EQ(handler.lastHeartbeatSeq, 55);
}

// ===========================================================================
// Unknown message type
// ===========================================================================

TEST_F(ProtocolRouterTest, DispatchUnknownType_ReturnsFalse)
{
    EXPECT_FALSE(router.dispatch(static_cast<MessageType>(0xFF), 1, nullptr, 0));
}

TEST_F(ProtocolRouterTest, DispatchDeviceMessageType_ReturnsFalse)
{
    // ACK (0x81) is a device→host message — should not be dispatched.
    EXPECT_FALSE(router.dispatch(MessageType::ACK, 1, nullptr, 0));
}

// ===========================================================================
// Null handler
// ===========================================================================

TEST_F(ProtocolRouterTest, NullHandler_ReturnsFalse)
{
    ProtocolRouter<Cfg> nullRouter(nullptr);
    EXPECT_FALSE(nullRouter.dispatch(MessageType::HEARTBEAT, 1, nullptr, 0));
}
