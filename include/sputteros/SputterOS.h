#ifndef SPUTTEROS_SPUTTEROS_H
#define SPUTTEROS_SPUTTEROS_H

/**
 * @file SputterOS.h
 * @brief Master umbrella header — includes the entire SputterOS public API.
 *
 * Usage:
 * @code
 * #include "sputteros/SputterOS.h"
 * @endcode
 *
 * For finer-grained control, include individual component headers instead:
 *  - `sputteros/Kernel.h`     — System singleton, kernel state, tasks
 *  - `sputteros/Builder.h`    — Fluent SystemBuilder API
 *  - `sputteros/HAL.h`        — Hardware abstraction interfaces
 *  - `sputteros/OSAL.h`       — OS abstraction: tasks, sync, time
 *  - `sputteros/Logic.h`      — Command parsing, interlocks, safety
 *  - `sputteros/Comms.h`      — CLI communications bridge
 *  - `sputteros/Utils.h`      — PID, timing, memory profiling, logging
 *  - `sputteros/Interfaces.h` — User-facing extension interfaces
 *
 * @author SputterOS Contributors
 * @date 4/11/2026
 */

// ── Core Configuration ──────────────────────────────────────────────────────
#include "sputteros/ConfigTraits.h"
#include "sputteros/OpResult.h"

// ── Component Headers ───────────────────────────────────────────────────────
#include "sputteros/Builder.h"
#include "sputteros/Comms.h"
#include "sputteros/HAL.h"
#include "sputteros/Interfaces.h"
#include "sputteros/Kernel.h"
#include "sputteros/Logic.h"
#include "sputteros/OSAL.h"
#include "sputteros/Utils.h"

#endif // SPUTTEROS_SPUTTEROS_H
