#ifndef SPUTTEROS_COMMS_PROTOCOL_IPROTOCOLHANDLER_H
#define SPUTTEROS_COMMS_PROTOCOL_IPROTOCOLHANDLER_H

/**
 * @file IProtocolHandler.h
 * @brief Interface for handling decoded framed-protocol messages.
 *
 * When the FrameDecoder produces a valid frame, the ProtocolRouter
 * deserializes the payload and dispatches it as a typed call on this
 * interface. Concrete implementations (e.g., ScheduledCommsTask)
 * handle commands, handshake lifecycle, metrics requests, etc.
 *
 * @tparam Cfg Configuration struct providing `Command`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/12/2026
 */

#include <cstdint>

namespace SputterOS
{

/**
 * @brief Callback interface for decoded framed-protocol messages.
 *
 * Non-copyable following SputterOS kernel patterns.
 *
 * @tparam Cfg Configuration struct providing `Command`.
 */
template <typename Cfg> class IProtocolHandler
{
  public:
    using CommandStruct = typename Cfg::Command;

    IProtocolHandler(const IProtocolHandler &)            = delete;
    IProtocolHandler &operator=(const IProtocolHandler &) = delete;
    virtual ~IProtocolHandler()                           = default;

    /**
     * @brief Called when a COMMAND frame is received.
     * @param cmd The deserialized command packet.
     * @param seqNum Frame sequence number for ACK/NACK correlation.
     */
    virtual void onCommand(const CommandStruct &cmd, uint8_t seqNum) = 0;

    /**
     * @brief Called when a HANDSHAKE_REQ frame is received.
     * @param version Protocol version requested by the host.
     * @param seqNum Frame sequence number.
     */
    virtual void onHandshakeRequest(uint16_t version, uint8_t seqNum) = 0;

    /**
     * @brief Called when an EXIT_HANDSHAKE frame is received.
     * @param seqNum Frame sequence number.
     */
    virtual void onExitHandshake(uint8_t seqNum) = 0;

    /**
     * @brief Called when a METRICS_REQ frame is received.
     * @param seqNum Frame sequence number.
     */
    virtual void onMetricsRequest(uint8_t seqNum) = 0;

    /**
     * @brief Called when a HEARTBEAT frame is received.
     * @param seqNum Frame sequence number (echo back).
     */
    virtual void onHeartbeat(uint8_t seqNum) = 0;

  protected:
    IProtocolHandler() = default;
};

} // namespace SputterOS

#endif // SPUTTEROS_COMMS_PROTOCOL_IPROTOCOLHANDLER_H
