#ifndef SPUTTEROS_UTILS_H
#define SPUTTEROS_UTILS_H

/**
 * @file Utils.h
 * @brief Utilities component header — PID, timing, memory, logging.
 *
 * Includes all utility classes: PID controller, non-blocking stopwatch,
 * memory profiler, and the logging subsystem (ErrorLogger,
 * TelemetryLogger, LightweightStringBuilder).
 *
 * @code
 * #include "sputteros/Utils.h"
 * @endcode
 *
 * @author SputterOS Contributors
 * @date 4/11/2026
 */

// ── Process Control ─────────────────────────────────────────────────────────
#include "sputteros/utils/MemoryProfiler.h"
#include "sputteros/utils/NonBlockingStopwatch.h"
#include "sputteros/utils/PIDController.h"

// ── Logging ─────────────────────────────────────────────────────────────────
#include "sputteros/utils/logging/ErrorLogger.h"
#include "sputteros/utils/logging/LightweightStringBuilder.h"
#include "sputteros/utils/logging/TelemetryLogger.h"

#endif // SPUTTEROS_UTILS_H
