#ifndef SPUTTEROS_COMMS_PROTOCOL_PROTOCOLROUTER_H
#define SPUTTEROS_COMMS_PROTOCOL_PROTOCOLROUTER_H

/**
 * @file ProtocolRouter.h
 * @brief Deserializes decoded frame payloads and dispatches to IProtocolHandler.
 *
 * After the FrameDecoder produces a valid frame, the ProtocolRouter
 * inspects the MessageType, deserializes the payload into typed arguments,
 * and calls the corresponding method on the IProtocolHandler.
 *
 * Payload schemas:
 * - COMMAND:        [CmdID:1][targetDevice:1][value:4 LE]  (6 bytes)
 * - HANDSHAKE_REQ:  [version:2 LE][magic:4]                (6 bytes)
 * - EXIT_HANDSHAKE: (empty)                                 (0 bytes)
 * - METRICS_REQ:    (empty)                                 (0 bytes)
 * - HEARTBEAT:      (empty)                                 (0 bytes)
 *
 * @tparam Cfg Configuration struct providing `Command`, `CmdID`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/12/2026
 */

#include "sputteros/comms/protocol/FrameConstants.h"
#include "sputteros/comms/protocol/IProtocolHandler.h"
#include "sputteros/comms/protocol/MessageType.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace SputterOS
{

/**
 * @brief Routes decoded frame payloads to an IProtocolHandler.
 *
 * @tparam Cfg Configuration struct.
 */
template <typename Cfg> class ProtocolRouter
{
  public:
    using CommandStruct = typename Cfg::Command;
    using CmdID         = typename Cfg::CmdID;

    /**
     * @brief Construct a router that dispatches to the given handler.
     * @param handler Borrowed pointer to the protocol handler.
     */
    explicit ProtocolRouter(IProtocolHandler<Cfg> *handler) : m_handler(handler) {}

    /**
     * @brief Dispatch a decoded frame to the handler.
     *
     * @param type       Message type from the frame header.
     * @param seqNum     Sequence number from the frame header.
     * @param payload    Pointer to the decoded payload bytes.
     * @param payloadLen Length of the payload in bytes.
     * @return true if the message was successfully dispatched, false if
     *         the type was unknown or the payload was malformed.
     */
    bool dispatch(MessageType type, uint8_t seqNum, const uint8_t *payload, std::size_t payloadLen)
    {
        if (!m_handler)
        {
            return false;
        }

        switch (type)
        {
        case MessageType::COMMAND:
            return dispatchCommand(seqNum, payload, payloadLen);

        case MessageType::HANDSHAKE_REQ:
            return dispatchHandshake(seqNum, payload, payloadLen);

        case MessageType::EXIT_HANDSHAKE:
            m_handler->onExitHandshake(seqNum);
            return true;

        case MessageType::METRICS_REQ:
            m_handler->onMetricsRequest(seqNum);
            return true;

        case MessageType::HEARTBEAT:
            m_handler->onHeartbeat(seqNum);
            return true;

        default:
            return false; // Unknown or device→host type — ignore.
        }
    }

  private:
    IProtocolHandler<Cfg> *m_handler; /**< @brief Borrowed handler pointer. */

    /** @brief Expected COMMAND payload size: CmdID(1) + device(1) + value(4). */
    static constexpr std::size_t kCommandPayloadSize = 6;

    /** @brief Expected HANDSHAKE_REQ payload size: version(2) + magic(4). */
    static constexpr std::size_t kHandshakePayloadSize = 6;

    /**
     * @brief Deserialize and dispatch a COMMAND payload.
     */
    bool dispatchCommand(uint8_t seqNum, const uint8_t *payload, std::size_t payloadLen)
    {
        if (payloadLen != kCommandPayloadSize)
        {
            return false;
        }

        CommandStruct cmd{};
        cmd.id           = static_cast<CmdID>(payload[0]);
        cmd.targetDevice = payload[1];

        float val;
        std::memcpy(&val, &payload[2], sizeof(float));
        cmd.value = val;

        m_handler->onCommand(cmd, seqNum);
        return true;
    }

    /**
     * @brief Deserialize and dispatch a HANDSHAKE_REQ payload.
     */
    bool dispatchHandshake(uint8_t seqNum, const uint8_t *payload, std::size_t payloadLen)
    {
        if (payloadLen != kHandshakePayloadSize)
        {
            return false;
        }

        const uint16_t version = static_cast<uint16_t>(payload[0]) |
                                 (static_cast<uint16_t>(payload[1]) << 8);

        const uint32_t magic = static_cast<uint32_t>(payload[2]) |
                               (static_cast<uint32_t>(payload[3]) << 8) |
                               (static_cast<uint32_t>(payload[4]) << 16) |
                               (static_cast<uint32_t>(payload[5]) << 24);

        if (magic != kHandshakeMagic)
        {
            return false;
        }

        m_handler->onHandshakeRequest(version, seqNum);
        return true;
    }
};

} // namespace SputterOS

#endif // SPUTTEROS_COMMS_PROTOCOL_PROTOCOLROUTER_H
