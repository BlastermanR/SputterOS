#ifndef SPUTTEROS_OSAL_ATOMICDOUBLEBUFFER_H
#define SPUTTEROS_OSAL_ATOMICDOUBLEBUFFER_H

/**
 * @file AtomicDoubleBuffer.h
 * @brief Wait-free, latest-value double buffer for cross-core data sharing.
 *
 * `AtomicDoubleBuffer<T, CachePolicy>` provides a single-writer /
 * single-reader (SWSR) channel that always delivers the most recently
 * written value without any locking or blocking.  The underlying
 * mechanism is an atomic index swap rather than a seqlock, which keeps
 * the critical path to one `load` + one `store`.
 *
 * ### Design Constraints
 * - **Wait-free.** Both `write()` and `read()` complete in bounded time
 *   with no spin-loops or mutexes.
 * - **Latest-value only.** If the writer calls `write()` faster than the
 *   reader calls `read()`, intermediate values are silently discarded.
 *   This is intentional: do **not** use `AtomicDoubleBuffer` for
 *   reliable command delivery — use `LockFreeQueue` instead.
 * - **Zero heap.** Internal storage is two inline `T` objects.
 * - **Non-copyable.** Copying a live double-buffer would race on its
 *   atomic index.
 *
 * ### CachePolicy Hook
 * On platforms with non-coherent D-cache (e.g. STM32H7, ESP32-S3), the
 * writer must flush the inactive slot to RAM after writing and the reader
 * must invalidate the active slot before reading.  Provide a struct with
 * two static methods to satisfy the policy:
 *
 * @code
 * struct MyCachePolicy {
 *     static void flushBuffer(const void* addr, std::size_t bytes);
 *     static void invalidateBuffer(const void* addr, std::size_t bytes);
 * };
 * AtomicDoubleBuffer<SensorData, MyCachePolicy> buf;
 * @endcode
 *
 * The default `NoCachePolicy` provides empty (no-op) implementations that
 * compile away entirely — zero cost on cache-coherent hosts.
 *
 * ### Thread-Safety Contract
 * - Exactly **one** context may call `write()` at a time (producer).
 * - Exactly **one** context may call `read()` at a time (consumer).
 * - Producer and consumer may run concurrently on different cores.
 *
 * @tparam T             Value type.  Must be default-constructible and
 *                       copyable.
 * @tparam CachePolicy   Policy struct providing `flushBuffer` and
 *                       `invalidateBuffer` static methods.  Defaults to
 *                       `NoCachePolicy` (cache-coherent / host platforms).
 *
 * @author SputterOS Contributors
 * @date 4/13/2026
 */

#include <atomic>
#include <cstddef>

namespace SputterOS
{

// ===========================================================================
// NoCachePolicy — zero-cost default for cache-coherent platforms
// ===========================================================================

/**
 * @brief Default CachePolicy that performs no cache-maintenance operations.
 *
 * Suitable for all cache-coherent platforms (host, RP2040, most Cortex-M0/M4
 * without MPU-managed D-cache).  All methods are empty `inline` statics and
 * are eliminated entirely by the optimizer.
 *
 * @note Replace this policy with a platform-specific implementation when
 *       targeting a device whose D-cache is not automatically coherent
 *       between cores (e.g. STM32H7 with D-cache enabled, ESP32-S3 PSRAM).
 */
struct NoCachePolicy
{
    /**
     * @brief No-op flush — does nothing on cache-coherent platforms.
     * @param addr  Unused start address of the region to flush.
     * @param bytes Unused byte length of the region.
     */
    static void flushBuffer(const void * /*addr*/, std::size_t /*bytes*/) {}

    /**
     * @brief No-op invalidate — does nothing on cache-coherent platforms.
     * @param addr  Unused start address of the region to invalidate.
     * @param bytes Unused byte length of the region.
     */
    static void invalidateBuffer(const void * /*addr*/, std::size_t /*bytes*/) {}
};

// ===========================================================================
// AtomicDoubleBuffer
// ===========================================================================

/**
 * @brief Wait-free SWSR double buffer for cross-core latest-value sharing.
 *
 * @tparam T            Value type (default-constructible, copyable).
 * @tparam CachePolicy  Cache maintenance policy.  Defaults to `NoCachePolicy`.
 *
 * @code
 * AtomicDoubleBuffer<SensorReading> buf;
 *
 * // Producer (Core 1):
 * SensorReading r = sample();
 * buf.write(r);
 *
 * // Consumer (Core 0):
 * SensorReading latest = buf.read();
 * @endcode
 */
template <typename T, typename CachePolicy = NoCachePolicy> class AtomicDoubleBuffer
{
  public:
    // -----------------------------------------------------------------------
    // Construction / destruction
    // -----------------------------------------------------------------------

    /**
     * @brief Default-constructs both internal slots to `T{}`.
     *
     * The initial readable index is 0, so the first `read()` before any
     * `write()` will return a value-initialised `T`.
     */
    AtomicDoubleBuffer() = default;

    /// @cond — suppress Doxygen for deleted copy members
    AtomicDoubleBuffer(const AtomicDoubleBuffer &)            = delete;
    AtomicDoubleBuffer &operator=(const AtomicDoubleBuffer &) = delete;
    /// @endcond

    // -----------------------------------------------------------------------
    // Core operations
    // -----------------------------------------------------------------------

    /**
     * @brief Writes a new value, making it immediately visible to the reader.
     *
     * Selects the inactive slot (the one not currently being read), copies
     * `value` into it, flushes the slot to RAM via `CachePolicy::flushBuffer`,
     * then atomically publishes the slot index with `memory_order_release`.
     *
     * @param value  The value to publish to the consumer.
     *
     * @note May only be called from the single designated producer context.
     * @note If the consumer does not call `read()` before the next `write()`,
     *       the intermediate value is silently discarded — this is by design.
     */
    void write(const T &value)
    {
        const std::size_t next = m_writeIndex.load(std::memory_order_relaxed) ^ 1U;
        m_buffers[next]        = value;
        CachePolicy::flushBuffer(&m_buffers[next], sizeof(T));
        m_writeIndex.store(next, std::memory_order_release);
    }

    /**
     * @brief Returns the most recently written value.
     *
     * Loads the published index with `memory_order_acquire` (synchronises
     * with the producer's `memory_order_release` store), invalidates the
     * slot via `CachePolicy::invalidateBuffer`, then returns a copy of the
     * slot's current value.
     *
     * @return Copy of the latest value written by the producer, or `T{}`
     *         if `write()` has never been called.
     *
     * @note May only be called from the single designated consumer context.
     */
    T read() const
    {
        const std::size_t idx = m_writeIndex.load(std::memory_order_acquire);
        CachePolicy::invalidateBuffer(&m_buffers[idx], sizeof(T));
        return m_buffers[idx];
    }

  private:
    // -----------------------------------------------------------------------
    // Storage
    // -----------------------------------------------------------------------

    /// @brief Dual storage slots; slot `m_writeIndex` holds the latest value.
    T m_buffers[2]{};

    /**
     * @brief Index of the slot containing the most recently written value.
     *
     * Written with `memory_order_release` by the producer; read with
     * `memory_order_acquire` by the consumer to establish the happens-before
     * relationship that makes the payload visible without additional fencing.
     */
    std::atomic<std::size_t> m_writeIndex{0};
};

} // namespace SputterOS

#endif // SPUTTEROS_OSAL_ATOMICDOUBLEBUFFER_H
