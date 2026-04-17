#ifndef SPUTTEROS_EXAMPLES_COMMON_OSAL_TIMEDMUTEXADAPTER_H
#define SPUTTEROS_EXAMPLES_COMMON_OSAL_TIMEDMUTEXADAPTER_H

/**
 * @file TimedMutexAdapter.h
 * @brief Shared IMutex implementation using `std::timed_mutex` for OS-native builds.
 *
 * Wraps `std::timed_mutex` to satisfy the SputterOS `IMutex` interface with
 * proper bounded-wait semantics via `try_lock_for()`.
 *
 * @note This adapter is for host / desktop testing only.  Embedded targets
 *       should provide their own `IMutex` wrapping the platform mutex
 *       (e.g. FreeRTOS `xSemaphoreTake` with `pdMS_TO_TICKS` timeout).
 *
 * @author Ryan Massie (rmassie)
 * @date 4/16/2026
 */

#include "sputteros/osal/sync/IMutex.h"

#include <chrono>
#include <mutex>

namespace ExamplesCommon
{

class TimedMutexAdapter final : public SputterOS::IMutex
{
  public:
    bool lock(SputterOS::SputterMillis timeoutMs) override
    {
        return m_mtx.try_lock_for(std::chrono::milliseconds{timeoutMs});
    }

    bool try_lock() override { return m_mtx.try_lock(); }

    void unlock() override { m_mtx.unlock(); }

  private:
    std::timed_mutex m_mtx;
};

} // namespace ExamplesCommon

#endif // SPUTTEROS_EXAMPLES_COMMON_OSAL_TIMEDMUTEXADAPTER_H
