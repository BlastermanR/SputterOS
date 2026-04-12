#ifndef SPUTTEROS_COMMS_PROTOCOL_FRAMEENCODER_H
#define SPUTTEROS_COMMS_PROTOCOL_FRAMEENCODER_H

/**
 * @file FrameEncoder.h
 * @brief Encodes structured messages into COBS-framed wire bytes.
 *
 * Builds a raw frame (header + payload + CRC16) in an internal staging
 * buffer, COBS-encodes the result, and appends the 0x00 delimiter.
 * All storage is stack-local (zero-heap).
 *
 * @tparam MaxPayload Maximum payload size in bytes (controls buffer sizing).
 *
 * @author Ryan Massie (rmassie)
 * @date 4/12/2026
 */

#include "sputteros/comms/protocol/CobsCodec.h"
#include "sputteros/comms/protocol/Crc16.h"
#include "sputteros/comms/protocol/FrameConstants.h"
#include "sputteros/comms/protocol/MessageType.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace SputterOS
{

/**
 * @brief Encodes messages into COBS-framed wire format.
 *
 * Usage:
 * @code
 *   FrameEncoder<256> enc;
 *   uint8_t wire[FrameEncoder<256>::kMaxWireSize];
 *   std::size_t n = enc.encode(MessageType::ACK, seq, payload, payLen, wire, sizeof(wire));
 *   stream.write(wire, n);
 * @endcode
 *
 * @tparam MaxPayload Upper bound on payload size. Determines internal buffer sizes.
 */
template <std::size_t MaxPayload = 256> class FrameEncoder
{
  public:
    /** @brief Maximum raw frame size (header + max payload + CRC). */
    static constexpr std::size_t kMaxRawSize = kFrameOverhead + MaxPayload;

    /** @brief Maximum wire size after COBS encoding + delimiter. */
    static constexpr std::size_t kMaxWireSize = cobsMaxEncodedLen(kMaxRawSize) + 1;

    /**
     * @brief Encode a message into wire format.
     *
     * @param type       Message type identifier.
     * @param seqNum     Sequence number for ACK correlation.
     * @param payload    Pointer to payload bytes (may be nullptr if payloadLen == 0).
     * @param payloadLen Number of payload bytes.
     * @param outBuf     Destination buffer for the wire-encoded frame.
     * @param outCap     Capacity of `outBuf` in bytes.
     * @return Number of bytes written to `outBuf`, or 0 on error.
     */
    std::size_t encode(MessageType type, uint8_t seqNum, const uint8_t *payload, std::size_t payloadLen,
                       uint8_t *outBuf, std::size_t outCap) const
    {
        if (payloadLen > MaxPayload)
        {
            return 0;
        }

        const std::size_t rawLen = kFrameOverhead + payloadLen;
        if (outCap < cobsMaxEncodedLen(rawLen) + 1)
        {
            return 0;
        }

        // -- Build raw frame in staging buffer --
        uint8_t raw[kMaxRawSize];
        std::size_t pos = 0;

        // Header: [MsgType:1][SeqNum:1][PayloadLen:2 LE]
        raw[pos++] = static_cast<uint8_t>(type);
        raw[pos++] = seqNum;
        raw[pos++] = static_cast<uint8_t>(payloadLen & 0xFF);
        raw[pos++] = static_cast<uint8_t>((payloadLen >> 8) & 0xFF);

        // Payload
        if (payloadLen > 0 && payload != nullptr)
        {
            std::memcpy(&raw[pos], payload, payloadLen);
            pos += payloadLen;
        }

        // Trailer: CRC16 over header + payload
        const uint16_t crc = crc16(raw, pos);
        raw[pos++] = static_cast<uint8_t>(crc & 0xFF);
        raw[pos++] = static_cast<uint8_t>((crc >> 8) & 0xFF);

        // -- COBS-encode --
        const std::size_t cobsLen = Cobs::encode(raw, pos, outBuf, outCap - 1);
        if (cobsLen == 0)
        {
            return 0;
        }

        // -- Append frame delimiter --
        outBuf[cobsLen] = kFrameDelimiter;

        return cobsLen + 1;
    }
};

} // namespace SputterOS

#endif // SPUTTEROS_COMMS_PROTOCOL_FRAMEENCODER_H
