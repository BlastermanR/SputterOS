#ifndef SPUTTEROS_COMMS_PROTOCOL_CRC16_H
#define SPUTTEROS_COMMS_PROTOCOL_CRC16_H

/**
 * @file Crc16.h
 * @brief CRC16-CCITT (0x1021) implementation for frame integrity checks.
 *
 * Uses the CCITT polynomial 0x1021 with configurable initial value
 * (default 0xFFFF). Bit-shift implementation — no lookup table, keeping
 * flash footprint minimal on embedded targets.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/12/2026
 */

#include <cstddef>
#include <cstdint>

namespace SputterOS
{

// =========================================================================
// CRC16-CCITT
// =========================================================================

/**
 * @brief Compute CRC16-CCITT over a byte buffer.
 *
 * @param data Pointer to the input data.
 * @param len  Number of bytes.
 * @param init Initial CRC value (default 0xFFFF).
 * @return Computed 16-bit CRC.
 */
inline uint16_t crc16(const uint8_t *data, std::size_t len, uint16_t init = 0xFFFF)
{
    uint16_t crc = init;

    for (std::size_t i = 0; i < len; ++i)
    {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int j = 0; j < 8; ++j)
        {
            if (crc & 0x8000)
            {
                crc = (crc << 1) ^ 0x1021;
            }
            else
            {
                crc = crc << 1;
            }
        }
    }

    return crc;
}

} // namespace SputterOS

#endif // SPUTTEROS_COMMS_PROTOCOL_CRC16_H
