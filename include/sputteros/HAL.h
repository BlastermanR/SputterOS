#ifndef SPUTTEROS_HAL_H
#define SPUTTEROS_HAL_H

/**
 * @file HAL.h
 * @brief Hardware Abstraction Layer component header.
 *
 * Provides the device and stream interfaces that platform-specific
 * code must implement.
 *
 * @code
 * #include "sputteros/HAL.h"
 * @endcode
 *
 * @author SputterOS Contributors
 * @date 4/11/2026
 */

// ── Device Interfaces ───────────────────────────────────────────────────────
#include "sputteros/hal/base/ISputterDevice.h"
#include "sputteros/hal/devices/IStream.h"

#endif // SPUTTEROS_HAL_H
