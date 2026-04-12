#ifndef SPUTTEROS_INTERFACES_H
#define SPUTTEROS_INTERFACES_H

/**
 * @file Interfaces.h
 * @brief User-facing extension interfaces header.
 *
 * Includes the interfaces that application code typically implements:
 * `IUserApplication`, `ISafetyMonitor`, and `IProcessState`.
 *
 * @code
 * #include "sputteros/Interfaces.h"
 * @endcode
 *
 * @author SputterOS Contributors
 * @date 4/11/2026
 */

// ── Application Interfaces ──────────────────────────────────────────────────
#include "sputteros/interfaces/IProcessState.h"
#include "sputteros/kernel/interfaces/ISafetyMonitor.h"
#include "sputteros/kernel/interfaces/IUserApplication.h"

#endif // SPUTTEROS_INTERFACES_H
