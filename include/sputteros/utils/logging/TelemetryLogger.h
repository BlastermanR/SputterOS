#ifndef SPUTTEROS_UTILS_TELEMETRYLOGGER_H
#define SPUTTEROS_UTILS_TELEMETRYLOGGER_H

/**
 * @file TelemetryLogger.h
 * @brief Runtime log buffer for human-readable task telemetry.
 *
 * Provides a task-aware log channel that buffers timestamped messages
 * in a fixed-size ring buffer and drains them via a caller-supplied
 * write callback on demand. Unlike `ErrorLogger`, which is a fault
 * post-mortem buffer for diagnostic codes, `TelemetryLogger` carries
 * human-readable status text intended for a live operator terminal.
 *
 * Messages are tagged with a `TaskID` so the operator can identify
 * which task produced each line (e.g. `[ControlTask]`, `[CommsTask]`).
 * A configurable `Verbosity` filter suppresses messages below the
 * current threshold at drain time, leaving the ring buffer intact.
 *
 * Optional cross-core/ISR locking: If a mutex is provided at construction,
 * all public operations (`log()`, `drain()`, `setVerbosity()`, `count()`,
 * `clear()`) are protected against concurrent access. This makes
 * TelemetryLogger safe for multi-core or multi-task scenarios. If no
 * mutex is provided, TelemetryLogger is single-threaded only.
 *
 * Output format per entry:
 * @code
 *   [<timestamp_ms>][<TaskName>] <text>\n
 * @endcode
 *
 * @note ISR-safe if guard mutex provides ISR-safe semantics (e.g.
 *       `PicoCriticalSection`). Calls from multiple tasks are safe if
 *       guarded by an appropriate mutex.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */

#include "sputteros/osal/SputterTime.h"
#include "sputteros/osal/sync/IMutex.h"
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace SputterOS
{

class TelemetryLogger
{
  public:
    // -----------------------------------------------------------------------
    // Public constants
    // -----------------------------------------------------------------------

    /** @brief Maximum characters in a single log message (including null). */
    static constexpr std::size_t kTextLen = 64;

    /** @brief Fixed capacity of the ring buffer (entries, not bytes). */
    static constexpr std::size_t kCapacity = 32;

    /**
     * @brief Write-callback type for drain().
     *
     * Decouples TelemetryLogger from any specific HAL stream interface.
     * The callback writes formatted log data to whatever output sink
     * the caller owns (USB, UART, stdout, etc.).
     *
     * @param data: Pointer to the formatted bytes to write.
     * @param len:  Number of bytes.
     * @param ctx:  Opaque caller context (e.g. pointer to a stream object).
     */
    using DrainWriteFn = void (*)(const uint8_t *data, std::size_t len, void *ctx);

    // -----------------------------------------------------------------------
    // Enumerations
    // -----------------------------------------------------------------------

    /**
     * @brief Runtime verbosity threshold.
     *
     * Messages with a level <= the current threshold are passed through
     * to the stream on `drain()`; higher-level messages are suppressed.
     */
    enum class Verbosity : uint8_t
    {
        CRITICAL = 0, /**< @brief System errors that require immediate attention. */
        STATUS   = 1, /**< @brief Normal operating status (default threshold). */
        INFO     = 2, /**< @brief Informational detail for debugging. */
        DEBUG    = 3, /**< @brief Verbose developer tracing. */
    };

    /**
     * @brief Identifies which task produced a log message.
     *
     * Replace core-number source tags from the legacy USBSerial component
     * with task-level identifiers. `SYSTEM` is provided as a catch-all
     * for messages produced outside a named task context.
     */
    enum class TaskID : uint8_t
    {
        CONTROL     = 0, /**< @brief Message from ControlTask. */
        COMMS       = 1, /**< @brief Message from CommsTask. */
        DIAGNOSTICS = 2, /**< @brief Message from DiagnosticsTask. */
        SYSTEM      = 3, /**< @brief Startup or cross-task system messages. */
    };

    // -----------------------------------------------------------------------
    // Entry type
    // -----------------------------------------------------------------------

    /**
     * @brief A single buffered log entry.
     */
    struct Entry
    {
        SputterMicros timestamp;      /**< @brief Monotonic system time supplied by the caller (µs). */
        TaskID        task;           /**< @brief Task that produced this message. */
        Verbosity     level;          /**< @brief Verbosity level of this message. */
        char          text[kTextLen]; /**< @brief Null-terminated message text. */
    };

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /**
     * @brief Construct a TelemetryLogger with an empty buffer and STATUS threshold.
     * @param guard: Optional mutex to protect all operations. If `nullptr`, the logger
     *        is single-threaded only.
     */
    explicit TelemetryLogger(IMutex *guard = nullptr);

    // -----------------------------------------------------------------------
    // Write API
    // -----------------------------------------------------------------------

    /**
     * @brief Buffer a log message.
     *
     * The message is always stored regardless of the current verbosity
     * threshold. Filtering applies only at `drain()` time. If the ring
     * buffer is full the oldest entry is silently overwritten.
     *
     * @param task:         Task producing this message.
     * @param text:         Null-terminated message string (truncated to kTextLen-1).
     * @param level:        Verbosity level of this message.
     * @param timestamp_ms: Caller-supplied monotonic timestamp in milliseconds.
     */
    void log(TaskID task, const char *text, Verbosity level, SputterMicros timestamp);

    // -----------------------------------------------------------------------
    // Drain API
    // -----------------------------------------------------------------------

    /**
     * @brief Write all buffered entries at or below the verbosity threshold
     *        via the write callback, then remove them from the buffer.
     *
     * Entries above the current verbosity threshold are also removed from
     * the buffer (they are discarded, not re-queued). This keeps the buffer
     * from accumulating suppressed messages.
     *
     * Format per entry written:
     * @code
     *   [<timestamp_ms>][<TaskName>] <text>\n
     * @endcode
     *
     * @param writeFn: Callback invoked for each formatted log line.
     * @param ctx:     Opaque context forwarded to every `writeFn` call.
     */
    void drain(DrainWriteFn writeFn, void *ctx = nullptr);

    // -----------------------------------------------------------------------
    // Verbosity control
    // -----------------------------------------------------------------------

    /**
     * @brief Set the verbosity filter threshold.
     * @param level: Messages at this level or below are printed on `drain()`.
     */
    void setVerbosity(Verbosity level);

    /**
     * @brief Return the current verbosity threshold.
     * @return Current verbosity level.
     */
    Verbosity getVerbosity() const;

    // -----------------------------------------------------------------------
    // Inspection
    // -----------------------------------------------------------------------

    /**
     * @brief Return the number of entries currently in the buffer.
     * @return Entry count in [0, kCapacity].
     */
    std::size_t count() const;

    /**
     * @brief Discard all buffered entries without draining.
     */
    void clear();

  private:
    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    /**
     * @brief Convert a TaskID to its human-readable label.
     * @param task: TaskID to convert.
     * @return Null-terminated label string.
     */
    static const char *taskName(TaskID task);

    // -----------------------------------------------------------------------
    // Internal synchronization
    // -----------------------------------------------------------------------

    IMutex *m_guard; /**< @brief Optional mutex guarding ring buffer state. */

    // -----------------------------------------------------------------------
    // Ring buffer state
    // -----------------------------------------------------------------------

    Entry       m_buf[kCapacity]; /**< @brief Circular storage array. */
    std::size_t m_head;           /**< @brief Write index (next slot to fill). */
    std::size_t m_tail;           /**< @brief Read index (oldest entry). */
    std::size_t m_count;          /**< @brief Number of valid entries. */
    Verbosity   m_verbosity;      /**< @brief Active verbosity threshold. */
};

// ============================================================================
// Convenience type aliases for reduced boilerplate across all users
// ============================================================================

/**
 * @brief Shorthand type alias for TelemetryLogger::TaskID.
 *
 * Usage:
 * @code
 *   using TaskID = SputterOS::TelemetryLoggerTaskID;
 *   auto log_task = TaskID::SYSTEM;
 * @endcode
 */
using TelemetryLoggerTaskID = TelemetryLogger::TaskID;

/**
 * @brief Shorthand type alias for TelemetryLogger::Verbosity.
 *
 * Usage:
 * @code
 *   using Verbosity = SputterOS::TelemetryLoggerVerbosity;
 *   auto level = Verbosity::DEBUG;
 * @endcode
 */
using TelemetryLoggerVerbosity = TelemetryLogger::Verbosity;

} // namespace SputterOS

#endif // SPUTTEROS_UTILS_TELEMETRYLOGGER_H
