#ifndef SPUTTEROS_UTILS_ERRORLOGGER_H
#define SPUTTEROS_UTILS_ERRORLOGGER_H

#include "sputteros/osal/SputterTime.h"
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace SputterOS
{

/**
 * @file ErrorLogger.h
 * @brief ISR-safe circular buffer for diagnostic telemetry.
 *
 * Stores state transitions, interlock trips, and fault codes in a
 * fixed-size ring buffer for post-mortem analysis. When the buffer is
 * full the oldest entry is silently overwritten (black-box behaviour).
 *
 * @note `log()` is safe to call from ISR context. `read()` and
 *       `count()` must only be called from task context. Thread safety
 *       is provided by `std::atomic` head/tail indices with
 *       acquire/release semantics, matching the `LockFreeQueue` model.
 *
 * @note When the buffer is full and `log()` overwrites the oldest entry,
 *       a concurrent `read()` may observe a stale tail for one cycle.
 *       This is acceptable for a diagnostic log buffer.
 *
 * @note Timestamps use `SputterMicros` (uint64_t microseconds).
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */
class ErrorLogger
{
  public:
    /**
     * @brief Severity and category codes for logged events.
     */
    enum class ErrorCode : uint8_t
    {
        STATE_TRANSITION = 0, /**< @brief System state changed. */
        INTERLOCK_TRIP   = 1, /**< @brief An interlock condition was violated. */
        SOFT_ABORT       = 2, /**< @brief Software abort was triggered. */
        HARD_FAULT       = 3, /**< @brief Latching hard fault was triggered. */
        ARC_DETECTED     = 4, /**< @brief Plasma arc event detected. */
        SENSOR_ERROR     = 5, /**< @brief A HAL sensor reported an error. */
        WATCHDOG_KICK    = 6, /**< @brief Watchdog timer was kicked. */
        TIMER_ROLLOVER   = 7, /**< @brief System timer wrapped around. */
        INVALID_STATE    = 8, /**< @brief Invalid kernel state transition attempted. */
        CRUNCH_OVERRUN   = 9  /**< @brief Crunch task iteration exceeded maxIterationUs(). */
    };

    /**
     * @brief A single log entry stored in the ring buffer.
     */
    struct Entry
    {
        ErrorCode     code;      /**< @brief Event category. */
        SputterMicros timestamp; /**< @brief Monotonic system time at log time (microseconds). */
        float         value;     /**< @brief Optional numeric payload (e.g. pressure reading). */
    };

    /**
     * @brief Construct an ErrorLogger with an empty ring buffer.
     */
    ErrorLogger();

    /**
     * @brief Log an event (ISR-safe, non-blocking).
     * @param code: Event category.
     * @param timestamp: Current monotonic time.
     * @param value: Optional payload value (default 0.0).
     *
     * @note If the buffer is full the oldest entry is overwritten.
     */
    void log(ErrorCode code, SputterMicros timestamp, float value = 0.0f);

    /**
     * @brief Dequeue the oldest entry from the buffer.
     * @param entry: Reference populated with the dequeued entry.
     * @return true if an entry was available, false if the buffer is empty.
     *
     * @note Not ISR-safe — call from task context only.
     */
    bool read(Entry &entry);

    /**
     * @brief Return the number of entries currently in the buffer.
     * @return Entry count (0 to `kCapacity`).
     */
    std::size_t count() const;

    /**
     * @brief Discard all buffered entries.
     */
    void clear();

  private:
    /**
     * --------------------
     * Ring Buffer State
     * --------------------
     */

    /** @brief Fixed capacity of the circular log buffer. */
    static constexpr std::size_t kCapacity = 32;

    /** @brief Internal slot count (kCapacity + 1 to disambiguate full from empty). */
    static constexpr std::size_t kSlots = kCapacity + 1;

    Entry                    m_buf[kSlots]; /**< @brief Circular storage array. */
    std::atomic<std::size_t> m_head;        /**< @brief Write index (producer/ISR). */
    std::atomic<std::size_t> m_tail;        /**< @brief Read index (consumer/task). */
};

}; // namespace SputterOS
#endif // SPUTTEROS_UTILS_ERRORLOGGER_H
