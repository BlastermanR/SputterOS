#ifndef SPUTTEROS_COMMS_PROTOCOL_FRAMEDECODER_H
#define SPUTTEROS_COMMS_PROTOCOL_FRAMEDECODER_H

/**
 * @file FrameDecoder.h
 * @brief Stateful byte-by-byte COBS frame decoder with CRC validation.
 *
 * Feed incoming wire bytes one at a time via `feedByte()`. When a complete
 * 0x00-delimited frame arrives, the decoder COBS-decodes the accumulated
 * data, validates the CRC16, and exposes the header fields and payload.
 *
 * All storage is internal fixed-size arrays (zero-heap).
 *
 * @tparam MaxPayload Maximum expected payload size. Determines buffer sizing.
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

namespace SputterOS
{

/**
 * @brief Result codes returned by `FrameDecoder::feedByte()`.
 */
enum class DecoderResult : uint8_t
{
    INCOMPLETE,  /**< @brief Frame is still accumulating bytes. */
    FRAME_READY, /**< @brief A complete, CRC-valid frame is available. */
    ERROR,       /**< @brief Decode or CRC error — frame discarded, decoder reset. */
};

/**
 * @brief Stateful COBS frame decoder.
 *
 * Usage:
 * @code
 *   FrameDecoder<256> dec;
 *   for (uint8_t b : wireBytes) {
 *       auto r = dec.feedByte(b);
 *       if (r == DecoderResult::FRAME_READY) {
 *           auto type = dec.getMessageType();
 *           auto seq  = dec.getSeqNum();
 *           // process dec.getPayload(), dec.getPayloadLen()
 *       }
 *   }
 * @endcode
 *
 * @tparam MaxPayload Upper bound on payload size.
 */
template <std::size_t MaxPayload = 256> class FrameDecoder
{
  public:
    /** @brief Maximum raw frame size (header + max payload + CRC). */
    static constexpr std::size_t kMaxRawSize = kFrameOverhead + MaxPayload;

    /** @brief Maximum COBS-encoded frame size (excluding delimiter). */
    static constexpr std::size_t kMaxCobsSize = cobsMaxEncodedLen(kMaxRawSize);

    FrameDecoder() { reset(); }

    /**
     * @brief Feed a single wire byte into the decoder.
     *
     * @param b The next byte from the transport.
     * @return `FRAME_READY` when a valid frame is decoded,
     *         `ERROR` on decode/CRC failure (decoder auto-resets),
     *         `INCOMPLETE` otherwise.
     */
    DecoderResult feedByte(uint8_t b)
    {
        if (b == kFrameDelimiter)
        {
            if (m_cobsLen == 0)
            {
                // Empty frame (consecutive delimiters) — ignore, stay ready.
                return DecoderResult::INCOMPLETE;
            }
            return processFrame();
        }

        // Accumulate COBS-encoded byte.
        if (m_cobsLen < kMaxCobsSize)
        {
            m_cobsBuf[m_cobsLen++] = b;
            return DecoderResult::INCOMPLETE;
        }

        // Overflow — discard and wait for next delimiter to re-sync.
        m_cobsLen = 0;
        return DecoderResult::ERROR;
    }

    /**
     * @brief Reset the decoder state, discarding any partial frame.
     */
    void reset()
    {
        m_cobsLen    = 0;
        m_msgType    = MessageType::HEARTBEAT;
        m_seqNum     = 0;
        m_payloadLen = 0;
    }

    /** @brief Message type from the last successfully decoded frame. */
    MessageType getMessageType() const { return m_msgType; }

    /** @brief Sequence number from the last successfully decoded frame. */
    uint8_t getSeqNum() const { return m_seqNum; }

    /** @brief Pointer to the decoded payload bytes. */
    const uint8_t *getPayload() const { return m_payload; }

    /** @brief Number of payload bytes in the last decoded frame. */
    std::size_t getPayloadLen() const { return m_payloadLen; }

  private:
    /**
     * @brief Attempt to COBS-decode and validate a complete frame.
     * @return `FRAME_READY` on success, `ERROR` on failure.
     */
    DecoderResult processFrame()
    {
        // COBS-decode the accumulated bytes.
        uint8_t rawBuf[kMaxRawSize];
        const std::size_t rawLen = Cobs::decode(m_cobsBuf, m_cobsLen, rawBuf, sizeof(rawBuf));
        m_cobsLen = 0;

        if (rawLen < kFrameOverhead)
        {
            return DecoderResult::ERROR; // Too short for header + CRC
        }

        // Validate CRC16: computed over everything except the last 2 bytes.
        const std::size_t crcOffset  = rawLen - kFrameTrailerSize;
        const uint16_t    crcExpect  = static_cast<uint16_t>(rawBuf[crcOffset]) |
                                       (static_cast<uint16_t>(rawBuf[crcOffset + 1]) << 8);
        const uint16_t crcComputed = crc16(rawBuf, crcOffset);

        if (crcComputed != crcExpect)
        {
            return DecoderResult::ERROR;
        }

        // Extract header fields.
        m_msgType         = static_cast<MessageType>(rawBuf[0]);
        m_seqNum          = rawBuf[1];
        const uint16_t pl = static_cast<uint16_t>(rawBuf[2]) |
                            (static_cast<uint16_t>(rawBuf[3]) << 8);

        // Validate payload length consistency.
        if (static_cast<std::size_t>(pl) != crcOffset - kFrameHeaderSize)
        {
            return DecoderResult::ERROR;
        }
        if (pl > MaxPayload)
        {
            return DecoderResult::ERROR;
        }

        m_payloadLen = static_cast<std::size_t>(pl);
        for (std::size_t i = 0; i < m_payloadLen; ++i)
        {
            m_payload[i] = rawBuf[kFrameHeaderSize + i];
        }

        return DecoderResult::FRAME_READY;
    }

    uint8_t     m_cobsBuf[kMaxCobsSize]; /**< @brief Accumulation buffer for COBS bytes. */
    std::size_t m_cobsLen;               /**< @brief Current position in accumulation buffer. */

    MessageType m_msgType;               /**< @brief Decoded message type. */
    uint8_t     m_seqNum;                /**< @brief Decoded sequence number. */
    uint8_t     m_payload[MaxPayload];   /**< @brief Decoded payload bytes. */
    std::size_t m_payloadLen;            /**< @brief Length of decoded payload. */
};

} // namespace SputterOS

#endif // SPUTTEROS_COMMS_PROTOCOL_FRAMEDECODER_H
