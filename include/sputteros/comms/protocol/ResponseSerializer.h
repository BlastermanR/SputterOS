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
#include "sputteros/utils/PerformanceSnapshot.h"

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

// =========================================================================
// METRICS_RESP — compact PerformanceSnapshot serialization
// =========================================================================

/** @brief Fixed wire size of the metrics header (system-level fields). */
static constexpr std::size_t kMetricsHeaderSize = 66;

/** @brief Fixed wire size of one task entry (no histogram). */
static constexpr std::size_t kMetricsTaskSize = 26;

namespace detail
{

/** @brief Little-endian write helpers. */
inline void writeU8(uint8_t *&p, uint8_t v) { *p++ = v; }

inline void writeU16(uint8_t *&p, uint16_t v)
{
    std::memcpy(p, &v, 2);
    p += 2;
}

inline void writeU32(uint8_t *&p, uint32_t v)
{
    std::memcpy(p, &v, 4);
    p += 4;
}

inline void writeU64(uint8_t *&p, uint64_t v)
{
    std::memcpy(p, &v, 8);
    p += 8;
}

inline void writeF32(uint8_t *&p, float v)
{
    std::memcpy(p, &v, 4);
    p += 4;
}

} // namespace detail

/**
 * @brief Serialize a PerformanceSnapshot into a compact METRICS_RESP payload.
 *
 * Wire layout (little-endian):
 * @code
 *   Header (66 bytes):
 *     timestamp           : uint64  (8)
 *     coreCount           : uint8   (1)
 *     coreUtilization[4]  : float32 (16)
 *     queueDepth          : uint16  (2)
 *     queueMaxDepth       : uint16  (2)
 *     queueAvgDepth       : float32 (4)
 *     peakHeapUsed        : uint32  (4)
 *     freeHeap            : uint32  (4)
 *     stackHighWater      : uint32  (4)
 *     totalGapUs          : uint32  (4)
 *     maxGapUs            : uint32  (4)
 *     avgGapUs            : float32 (4)
 *     schedulerTickCount  : uint32  (4)
 *     totalOverruns       : uint16  (2)
 *     totalDeadlineMisses : uint16  (2)
 *     taskCount           : uint8   (1)
 *
 *   Per task (26 bytes each):
 *     taskIndex : uint8   (1)
 *     coreId    : uint8   (1)
 *     lastUs    : uint32  (4)
 *     minUs     : uint32  (4)
 *     maxUs     : uint32  (4)
 *     avgUs     : float32 (4)
 *     samples   : uint32  (4)
 *     overruns  : uint16  (2)
 *     misses    : uint16  (2)
 * @endcode
 *
 * @param snap   Snapshot to serialize.
 * @param outBuf Destination buffer.
 * @param outCap Capacity of destination buffer.
 * @return Number of bytes written, or 0 if the buffer is too small.
 */
inline std::size_t serializeMetrics(const PerformanceSnapshot &snap, uint8_t *outBuf, std::size_t outCap)
{
    const std::size_t needed = kMetricsHeaderSize + snap.taskCount * kMetricsTaskSize;
    if (outCap < needed)
    {
        return 0;
    }

    uint8_t *p = outBuf;
    using namespace detail;

    // ── System header ────────────────────────────────────────────────
    writeU64(p, snap.timestamp);
    writeU8(p, static_cast<uint8_t>(snap.coreCount));
    for (std::size_t i = 0; i < 4; ++i)
    {
        writeF32(p, snap.coreUtilization[i]);
    }
    writeU16(p, static_cast<uint16_t>(snap.queueDepth));
    writeU16(p, static_cast<uint16_t>(snap.queueMaxDepth));
    writeF32(p, snap.queueAvgDepth);
    writeU32(p, static_cast<uint32_t>(snap.peakHeapUsed));
    writeU32(p, static_cast<uint32_t>(snap.freeHeap));
    writeU32(p, static_cast<uint32_t>(snap.stackHighWater));
    writeU32(p, static_cast<uint32_t>(snap.totalGapUs));
    writeU32(p, static_cast<uint32_t>(snap.maxGapUs));
    writeF32(p, snap.avgGapUs);
    writeU32(p, snap.schedulerTickCount);
    writeU16(p, static_cast<uint16_t>(snap.totalOverruns));
    writeU16(p, static_cast<uint16_t>(snap.totalDeadlineMisses));
    writeU8(p, static_cast<uint8_t>(snap.taskCount));

    // ── Per-task entries ─────────────────────────────────────────────
    for (std::size_t t = 0; t < snap.taskCount; ++t)
    {
        const auto &ts = snap.tasks[t];
        writeU8(p, static_cast<uint8_t>(ts.taskIndex));
        writeU8(p, static_cast<uint8_t>(ts.coreId));
        writeU32(p, static_cast<uint32_t>(ts.lastUs));
        writeU32(p, static_cast<uint32_t>(ts.minUs));
        writeU32(p, static_cast<uint32_t>(ts.maxUs));
        writeF32(p, ts.avgUs);
        writeU32(p, ts.samples);
        writeU16(p, static_cast<uint16_t>(ts.overruns));
        writeU16(p, static_cast<uint16_t>(ts.misses));
    }

    return static_cast<std::size_t>(p - outBuf);
}

} // namespace ResponseSerializer
} // namespace SputterOS

#endif // SPUTTEROS_COMMS_PROTOCOL_RESPONSESERIALIZER_H
