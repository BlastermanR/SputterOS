#ifndef CRUNCHLOOP_OSAL_IMPL_TIMEDMUTEXADAPTER_H
#define CRUNCHLOOP_OSAL_IMPL_TIMEDMUTEXADAPTER_H

/**
 * @file TimedMutexAdapter.h
 * @brief IMutex implementation using `std::timed_mutex` for OS-native builds.
 *
 * Wraps `std::timed_mutex` to satisfy the SputterOS `IMutex` interface with
 * proper bounded-wait semantics via `try_lock_for()`.
 *
 * @note This adapter is for host / desktop testing only.  Embedded targets
 *       should provide their own `IMutex` wrapping the platform mutex
 *       (e.g. FreeRTOS `xSemaphoreTake` with `pdMS_TO_TICKS` timeout).
 *
 * @author Ryan Massie (rmassie)
 * @date 4/14/2026
 */

#include "sputteros/osal/sync/IMutex.h"

#include <mutex>

namespace CrunchLoop
{

/**
 * @brief `IMutex` adapter backed by `std::timed_mutex` with bounded waits.
 */
class TimedMutexAdapter final : public SputterOS::IMutex
{
  public:
    /**
     * @brief Acquire the mutex, blocking up to the given timeout.
     * @param timeout Maximum wait duration.
     * @return `true` if acquired, `false` on timeout.
     */
    bool lock(std::chrono::milliseconds timeout) override { return m_mtx.try_lock_for(timeout); }

    /**
     * @brief Non-blocking try-lock.
     * @return `true` if the mutex was acquired, `false` otherwise.
     */
    bool try_lock() override { return m_mtx.try_lock(); }

    /**
     * @brief Release the mutex.
     */
    void unlock() override { m_mtx.unlock(); }

  private:
    std::timed_mutex m_mtx; ///< Underlying timed mutex.
};

} // namespace CrunchLoop

#endif // CRUNCHLOOP_OSAL_IMPL_TIMEDMUTEXADAPTER_H
