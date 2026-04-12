#ifndef SPUTTEROS_COMMS_PROTOCOL_FRAMECONSTANTS_H
#define SPUTTEROS_COMMS_PROTOCOL_FRAMECONSTANTS_H

/**
 * @file FrameConstants.h
 * @brief Wire-format constants for the COBS-framed protocol.
 *
 * Frame layout (before COBS encoding):
 * @code
 *  [MsgType:1][SeqNum:1][PayloadLen:2 LE][Payload:N][CRC16:2]
 * @endcode
 *
 * On the wire (after COBS encoding):
 * @code
 *  COBS( header + payload + CRC16 ) + 0x00
 * @endcode
 *
 * The trailing 0x00 byte serves as the unambiguous frame delimiter.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/12/2026
 */

#include "sputteros/comms/protocol/CobsCodec.h"

#include <cstddef>
#include <cstdint>

namespace SputterOS
{

// =========================================================================
// Frame structure sizes
// =========================================================================

/** @brief Size of the frame header: MsgType(1) + SeqNum(1) + PayloadLen(2). */
static constexpr std::size_t kFrameHeaderSize = 4;

/** @brief Size of the frame trailer: CRC16(2). */
static constexpr std::size_t kFrameTrailerSize = 2;

/** @brief Total overhead bytes added to the payload (header + trailer). */
static constexpr std::size_t kFrameOverhead = kFrameHeaderSize + kFrameTrailerSize;

/** @brief The 0x00 byte used as the COBS frame delimiter on the wire. */
static constexpr uint8_t kFrameDelimiter = 0x00;

// =========================================================================
// Handshake magic
// =========================================================================

/** @brief Protocol version number (incremented on breaking changes). */
static constexpr uint16_t kProtocolVersion = 1;

/** @brief 4-byte magic value in HANDSHAKE_REQ payload for identification. */
static constexpr uint32_t kHandshakeMagic = 0x53504F53; // "SPOS" in ASCII

// =========================================================================
// Worst-case buffer sizing
// =========================================================================

/**
 * @brief Compute the raw (pre-COBS) frame size for a given payload length.
 * @param payloadLen Payload size in bytes.
 * @return Total raw frame size (header + payload + CRC).
 */
constexpr std::size_t rawFrameSize(std::size_t payloadLen) { return kFrameOverhead + payloadLen; }

/**
 * @brief Compute the maximum wire size for a frame with a given payload length.
 *
 * This accounts for COBS encoding overhead plus the trailing 0x00 delimiter.
 *
 * @param payloadLen Payload size in bytes.
 * @return Worst-case number of bytes on the wire.
 */
constexpr std::size_t maxWireSize(std::size_t payloadLen)
{
    return cobsMaxEncodedLen(rawFrameSize(payloadLen)) + 1; // +1 for 0x00 delimiter
}

} // namespace SputterOS

#endif // SPUTTEROS_COMMS_PROTOCOL_FRAMECONSTANTS_H
