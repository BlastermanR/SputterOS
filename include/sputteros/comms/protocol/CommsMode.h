#ifndef SPUTTEROS_COMMS_PROTOCOL_COMMSMODE_H
#define SPUTTEROS_COMMS_PROTOCOL_COMMSMODE_H

/**
 * @file CommsMode.h
 * @brief Communications mode enumeration for the dual-mode protocol.
 *
 * The comms system starts in TEXT mode (human-readable ASCII CLI).
 * A Python CLI or monitoring tool can negotiate FRAMED mode via
 * a COBS-encoded handshake frame.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/12/2026
 */

#include <cstdint>

namespace SputterOS
{

/**
 * @brief Active communications mode.
 */
enum class CommsMode : uint8_t
{
    TEXT,   /**< @brief Human-readable ASCII line protocol. */
    FRAMED, /**< @brief COBS-encoded binary framing protocol. */
};

} // namespace SputterOS

#endif // SPUTTEROS_COMMS_PROTOCOL_COMMSMODE_H
