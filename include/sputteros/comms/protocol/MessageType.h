#ifndef SPUTTEROS_COMMS_PROTOCOL_MESSAGETYPE_H
#define SPUTTEROS_COMMS_PROTOCOL_MESSAGETYPE_H

/**
 * @file MessageType.h
 * @brief Enumeration of all framed-protocol message types.
 *
 * Message IDs are partitioned by direction:
 * - `0x01–0x7F` : Host → Device (requests / commands)
 * - `0x81–0xBF` : Device → Host (responses / async data)
 * - `0xF0–0xFF` : Bidirectional (heartbeat, diagnostics)
 *
 * @author Ryan Massie (rmassie)
 * @date 4/12/2026
 */

#include <cstdint>

namespace SputterOS
{

/**
 * @brief Protocol message type identifiers for the COBS-framed protocol.
 *
 * Each frame on the wire carries exactly one MessageType in its header.
 */
enum class MessageType : uint8_t
{
    // ── Host → Device (0x0X) ─────────────────────────────────────────────
    COMMAND        = 0x01, /**< @brief Command packet (CmdID + device + value). */
    HANDSHAKE_REQ  = 0x02, /**< @brief Request to enter framed mode. */
    METRICS_REQ    = 0x03, /**< @brief Request a PerformanceSnapshot. */
    EXIT_HANDSHAKE = 0x04, /**< @brief Request to return to text mode. */

    // ── Device → Host (0x8X) ─────────────────────────────────────────────
    ACK            = 0x81, /**< @brief Positive acknowledgement for a command. */
    NACK           = 0x82, /**< @brief Negative acknowledgement (queue full). */
    HANDSHAKE_RESP = 0x83, /**< @brief Response confirming framed mode entry. */
    TELEMETRY      = 0x84, /**< @brief Periodic telemetry string payload. */
    METRICS_RESP   = 0x85, /**< @brief PerformanceSnapshot response. */
    LOG            = 0x86, /**< @brief Log / diagnostic text entry. */
    DATA           = 0x87, /**< @brief User-defined binary data blob. */
    PERF_DATA      = 0x88, /**< @brief Structured performance data block. */

    // ── Bidirectional (0xFX) ─────────────────────────────────────────────
    HEARTBEAT = 0xF0, /**< @brief Keepalive ping / pong. */
};

// =========================================================================
// Direction helpers
// =========================================================================

/**
 * @brief Check whether a message type is a host-to-device request.
 * @param t The message type to test.
 * @return true if `t` is in the 0x01–0x7F range.
 */
inline constexpr bool isHostMessage(MessageType t)
{
    const auto v = static_cast<uint8_t>(t);
    return v >= 0x01 && v <= 0x7F;
}

/**
 * @brief Check whether a message type is a device-to-host response.
 * @param t The message type to test.
 * @return true if `t` is in the 0x81–0xBF range.
 */
inline constexpr bool isDeviceMessage(MessageType t)
{
    const auto v = static_cast<uint8_t>(t);
    return v >= 0x81 && v <= 0xBF;
}

/**
 * @brief Check whether a message type is bidirectional.
 * @param t The message type to test.
 * @return true if `t` is in the 0xF0–0xFF range.
 */
inline constexpr bool isBidirectionalMessage(MessageType t)
{
    const auto v = static_cast<uint8_t>(t);
    return v >= 0xF0;
}

} // namespace SputterOS

#endif // SPUTTEROS_COMMS_PROTOCOL_MESSAGETYPE_H
