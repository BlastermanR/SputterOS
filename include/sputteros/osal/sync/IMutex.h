#ifndef SPUTTEROS_OSAL_IMUTEX_H
#define SPUTTEROS_OSAL_IMUTEX_H

#include "sputteros/osal/SputterTime.h"
#include <cstdint>

namespace SputterOS
{

/**
 * @file IMutex.h
 * @brief Interface for an OS abstraction mutex.
 *
 * Defines bounded lock/unlock semantics for the SputterOS synchronization layer.
 *
 * @deprecated Mutex-based synchronization is deprecated for cross-core
 *             communication. Use `LockFreeQueue<Cfg, N>` instead. IMutex is
 *             retained for multi-producer/single-consumer buffering (e.g.
 *             TelemetryLogger, where multiple tasks log and one task drains)
 *             because MPSC lock-free ring buffers require complex CAS loops
 *             unsuitable for safety-critical embedded code. New code MUST NOT
 *             use IMutex for inter-core command or data transfer.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/26
 *
 * @note Implementations MUST support bounded waits; infinite blocking is forbidden.
 * @note Implementations SHOULD support priority inheritance where the underlying
 *       platform provides it to avoid priority inversion.
 */

class IMutex
{
  public:
    /**
     * @brief Virtual destructor.
     */
    virtual ~IMutex() = default;

    /**
     * @brief Acquire the mutex, blocking up to the given timeout.
     * @param timeout Maximum time to wait in milliseconds. A value of 0
     *        indicates a non-blocking attempt (try-lock).
     * @return true if the mutex was successfully acquired, false on timeout or error.
     *
     * @note OSAL policy: callers must use bounded timeouts to avoid deadlocks.
     */
    virtual bool lock(SputterMillis timeoutMs) = 0;

    /**
     * @brief Attempt to acquire the mutex without blocking.
     * @return true if the mutex was acquired, false otherwise.
     *
     * Default implementation calls `lock(0)` and can be overridden for efficiency.
     */
    virtual bool try_lock() { return lock(0); }

    /**
     * @brief Release the mutex.
     */
    virtual void unlock() = 0;

    // Non-copyable to prevent accidental copies of mutex handles.
    IMutex(const IMutex &)            = delete;
    IMutex &operator=(const IMutex &) = delete;

  protected:
    IMutex() = default;
};

} // namespace SputterOS

#endif // SPUTTEROS_OSAL_IMUTEX_H
