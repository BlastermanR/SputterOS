#ifndef SPUTTEROS_COMMS_PROTOCOL_COBSCODEC_H
#define SPUTTEROS_COMMS_PROTOCOL_COBSCODEC_H

/**
 * @file CobsCodec.h
 * @brief Consistent Overhead Byte Stuffing (COBS) encoder and decoder.
 *
 * COBS eliminates all 0x00 bytes from a payload so that 0x00 can serve
 * as an unambiguous frame delimiter. Overhead is at most
 * ceil(len / 254) + 1 bytes for an input of `len` bytes.
 *
 * All functions are pure, zero-heap, and operate on caller-provided buffers.
 * A return value of 0 indicates an error (buffer too small or malformed
 * input).
 *
 * @see https://en.wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing
 *
 * @author Ryan Massie (rmassie)
 * @date 4/12/2026
 */

#include <cstddef>
#include <cstdint>

namespace SputterOS
{

// =========================================================================
// Compile-time helpers
// =========================================================================

/**
 * @brief Compute the worst-case COBS-encoded length for a given source length.
 * @param srcLen Length of the unencoded payload in bytes.
 * @return Maximum possible encoded length (excluding the trailing 0x00 delimiter).
 */
constexpr std::size_t cobsMaxEncodedLen(std::size_t srcLen)
{
    // Each 254-byte block adds one overhead byte, plus one initial overhead byte.
    return srcLen + (srcLen / 254) + 1;
}

// =========================================================================
// COBS Codec
// =========================================================================

namespace Cobs
{

/**
 * @brief COBS-encode a source buffer into the destination buffer.
 *
 * The output does **not** include the trailing 0x00 frame delimiter — the
 * caller must append it when placing the encoded data on the wire.
 *
 * @param src    Pointer to the source (unencoded) data.
 * @param srcLen Number of bytes to encode.
 * @param dst    Pointer to the destination (encoded) buffer.
 * @param dstCap Capacity of the destination buffer in bytes.
 * @return Number of bytes written to `dst`, or 0 on error (dstCap too small).
 */
inline std::size_t encode(const uint8_t *src, std::size_t srcLen, uint8_t *dst, std::size_t dstCap)
{
    if (dstCap < cobsMaxEncodedLen(srcLen))
    {
        return 0;
    }

    std::size_t writeIdx = 0;
    std::size_t codeIdx  = writeIdx++;
    uint8_t     code     = 1;

    for (std::size_t i = 0; i < srcLen; ++i)
    {
        if (src[i] == 0x00)
        {
            dst[codeIdx] = code;
            codeIdx      = writeIdx++;
            code         = 1;
        }
        else
        {
            dst[writeIdx++] = src[i];
            ++code;
            if (code == 0xFF)
            {
                dst[codeIdx] = code;
                codeIdx      = writeIdx++;
                code         = 1;
            }
        }
    }
    dst[codeIdx] = code;

    return writeIdx;
}

/**
 * @brief COBS-decode a buffer (without the trailing 0x00 delimiter).
 *
 * The input must be the raw COBS-encoded bytes between two 0x00 delimiters.
 * The trailing delimiter itself must **not** be included in `src`.
 *
 * @param src    Pointer to the encoded data.
 * @param srcLen Number of encoded bytes.
 * @param dst    Pointer to the destination (decoded) buffer.
 * @param dstCap Capacity of the destination buffer in bytes.
 * @return Number of decoded bytes written to `dst`, or 0 on error.
 */
inline std::size_t decode(const uint8_t *src, std::size_t srcLen, uint8_t *dst, std::size_t dstCap)
{
    if (srcLen == 0)
    {
        return 0;
    }

    std::size_t readIdx  = 0;
    std::size_t writeIdx = 0;

    while (readIdx < srcLen)
    {
        const uint8_t code = src[readIdx++];
        if (code == 0x00)
        {
            return 0; // Unexpected zero in encoded stream
        }

        const std::size_t dataBytes = static_cast<std::size_t>(code - 1);
        if (readIdx + dataBytes > srcLen)
        {
            return 0; // Truncated data
        }
        if (writeIdx + dataBytes > dstCap)
        {
            return 0; // Output buffer too small
        }

        for (std::size_t j = 0; j < dataBytes; ++j)
        {
            if (src[readIdx] == 0x00)
            {
                return 0; // Unexpected zero in data section
            }
            dst[writeIdx++] = src[readIdx++];
        }

        // If code < 0xFF the original byte was 0x00 (implicit zero),
        // unless we've reached the end of the input.
        if (code < 0xFF && readIdx < srcLen)
        {
            if (writeIdx >= dstCap)
            {
                return 0;
            }
            dst[writeIdx++] = 0x00;
        }
    }

    return writeIdx;
}

} // namespace Cobs
} // namespace SputterOS

#endif // SPUTTEROS_COMMS_PROTOCOL_COBSCODEC_H
