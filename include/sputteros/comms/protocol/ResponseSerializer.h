#ifndef SPUTTEROS_COMMS_PROTOCOL_RESPONSESERIALIZER_H
#define SPUTTEROS_COMMS_PROTOCOL_RESPONSESERIALIZER_H

/**
 * @file ResponseSerializer.h
 * @brief Serializes outgoing protocol messages into frame payloads.
 *
 * Each `serialize*()` function writes a payload into a caller-provided
 * buffer. The caller then passes the payload to FrameEncoder to produce
 * the final wire frame. All functions are zero-heap.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/12/2026
 */

#include "sputteros/comms/protocol/FrameConstants.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace SputterOS
{

/**
 * @brief Serialization helpers for outgoing protocol payloads.
 */
namespace ResponseSerializer
{

/**
 * @brief Serialize an ACK payload.
 *
 * Payload: [CmdID:1]  (1 byte)
 *
 * @param cmdId    The command ID being acknowledged.
 * @param outBuf   Destination buffer.
 * @param outCap   Capacity of destination buffer.
 * @return Number of bytes written (1), or 0 on error.
 */
inline std::size_t serializeAck(uint8_t cmdId, uint8_t *outBuf, std::size_t outCap)
{
    if (outCap < 1)
    {
        return 0;
    }
    outBuf[0] = cmdId;
    return 1;
}

/**
 * @brief Serialize a NACK payload.
 *
 * Payload: [CmdID:1][targetDevice:1][value:4 LE]  (6 bytes)
 *
 * @param cmdId        Command ID being rejected.
 * @param targetDevice Target device index.
 * @param value        Command value.
 * @param outBuf       Destination buffer.
 * @param outCap       Capacity of destination buffer.
 * @return Number of bytes written (6), or 0 on error.
 */
inline std::size_t serializeNack(uint8_t cmdId, uint8_t targetDevice, float value, uint8_t *outBuf,
                                 std::size_t outCap)
{
    if (outCap < 6)
    {
        return 0;
    }
    outBuf[0] = cmdId;
    outBuf[1] = targetDevice;
    std::memcpy(&outBuf[2], &value, sizeof(float));
    return 6;
}

/**
 * @brief Serialize a HANDSHAKE_RESP payload.
 *
 * Payload: [version:2 LE][magic:4]  (6 bytes)
 *
 * @param outBuf  Destination buffer.
 * @param outCap  Capacity of destination buffer.
 * @return Number of bytes written (6), or 0 on error.
 */
inline std::size_t serializeHandshakeResp(uint8_t *outBuf, std::size_t outCap)
{
    if (outCap < 6)
    {
        return 0;
    }
    outBuf[0] = static_cast<uint8_t>(kProtocolVersion & 0xFF);
    outBuf[1] = static_cast<uint8_t>((kProtocolVersion >> 8) & 0xFF);
    outBuf[2] = static_cast<uint8_t>(kHandshakeMagic & 0xFF);
    outBuf[3] = static_cast<uint8_t>((kHandshakeMagic >> 8) & 0xFF);
    outBuf[4] = static_cast<uint8_t>((kHandshakeMagic >> 16) & 0xFF);
    outBuf[5] = static_cast<uint8_t>((kHandshakeMagic >> 24) & 0xFF);
    return 6;
}

/**
 * @brief Serialize a command into the binary payload format.
 *
 * Payload: [CmdID:1][targetDevice:1][value:4 LE]  (6 bytes)
 *
 * Used for COMMAND frames in both directions.
 *
 * @param cmdId        Command identifier.
 * @param targetDevice Target device index.
 * @param value        Numeric value.
 * @param outBuf       Destination buffer.
 * @param outCap       Capacity (must be >= 6).
 * @return Number of bytes written (6), or 0 on error.
 */
inline std::size_t serializeCommand(uint8_t cmdId, uint8_t targetDevice, float value, uint8_t *outBuf,
                                    std::size_t outCap)
{
    if (outCap < 6)
    {
        return 0;
    }
    outBuf[0] = cmdId;
    outBuf[1] = targetDevice;
    std::memcpy(&outBuf[2], &value, sizeof(float));
    return 6;
}

/**
 * @brief Serialize user-defined data into a DATA payload (pass-through).
 *
 * @param data    Pointer to user data bytes.
 * @param dataLen Number of bytes.
 * @param outBuf  Destination buffer.
 * @param outCap  Capacity (must be >= dataLen).
 * @return Number of bytes written, or 0 on error.
 */
inline std::size_t serializeData(const uint8_t *data, std::size_t dataLen, uint8_t *outBuf, std::size_t outCap)
{
    if (outCap < dataLen || (dataLen > 0 && data == nullptr))
    {
        return 0;
    }
    std::memcpy(outBuf, data, dataLen);
    return dataLen;
}

/**
 * @brief Serialize a log string into a LOG payload.
 *
 * Payload: [level:1][text:N]  (1 + strlen bytes)
 *
 * @param level   Verbosity level (0=CRITICAL, 1=STATUS, 2=INFO, 3=DEBUG).
 * @param text    Null-terminated log string.
 * @param outBuf  Destination buffer.
 * @param outCap  Capacity.
 * @return Number of bytes written, or 0 on error.
 */
inline std::size_t serializeLog(uint8_t level, const char *text, uint8_t *outBuf, std::size_t outCap)
{
    if (!text)
    {
        return 0;
    }
    const std::size_t textLen = std::strlen(text);
    const std::size_t total   = 1 + textLen;
    if (outCap < total)
    {
        return 0;
    }
    outBuf[0] = level;
    std::memcpy(&outBuf[1], text, textLen);
    return total;
}

} // namespace ResponseSerializer
} // namespace SputterOS

#endif // SPUTTEROS_COMMS_PROTOCOL_RESPONSESERIALIZER_H
