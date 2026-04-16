#ifndef SPUTTEROS_KERNEL_CRUNCHDISPATCHER_H
#define SPUTTEROS_KERNEL_CRUNCHDISPATCHER_H

/**
 * @file CrunchDispatcher.h
 * @brief Core runtime for CRUNCH-mode cores.
 *
 * `CrunchDispatcher<Cfg>` manages the tight loop for an `ICrunchTask`
 * running on a dedicated core in `CoreDispatchMode::CRUNCH`. Each
 * iteration the dispatcher:
 *
 * 1. Checks the safety abort flag (`System<Cfg>::isSafetyAborted()`).
 * 2. Kicks the watchdog (`WatchdogSync` + optional platform kick).
 * 3. Dispatches `ICrunchTask::crunch(now)` with wall-clock timing.
 * 4. Detects overruns against `ICrunchTask::maxIterationUs()` and
 *    counts consecutive violations.
 * 5. If consecutive overruns reach `CfgCrunchMaxOverruns<Cfg>::value`,
 *    calls `onCrunchAbort()` and exits the loop.
 *
 * ### Thread-Safety Contract
 * - `runLoop()` must be called from the core it manages.
 * - `configure()` must be called before `runLoop()` and only from
 *   a single thread (i.e. during builder phase or `run()` entry).
 *
 * @tparam Cfg Configuration struct satisfying `ConfigValidator<Cfg>`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/13/2026
 */

#include "sputteros/ConfigTraits.h"
#include "sputteros/osal/SputterTime.h"
#include "sputteros/osal/tasks/ICrunchTask.h"
#include "sputteros/utils/logging/ErrorLogger.h"

#include <cstddef>
#include <cstdint>

namespace SputterOS
{

// Forward declarations — System<Cfg> provides safety abort, timer, watchdog.
template <typename Cfg> class System;

namespace Kernel
{

/**
 * @brief Tight-loop dispatcher for CRUNCH-mode cores.
 *
 * Non-copyable. Configured once via `configure()`, then driven by
 * `runLoop()` which blocks until the kernel exits an active state,
 * a stop condition fires, a safety abort is signalled, or the
 * overrun threshold is exceeded.
 *
 * @tparam Cfg Configuration struct satisfying `ConfigValidator<Cfg>`.
 */
template <typename Cfg> class CrunchDispatcher
{
  public:
    /** @brief Platform watchdog kick function pointer type. */
    using WatchdogKickFn = void (*)();

    CrunchDispatcher()                                    = default;
    CrunchDispatcher(const CrunchDispatcher &)            = delete;
    CrunchDispatcher &operator=(const CrunchDispatcher &) = delete;

    // =====================================================================
    // Configuration
    // =====================================================================

    /**
     * @brief Configure the dispatcher with its crunch task and watchdog.
     *
     * Must be called before `runLoop()`. Called by `System<Cfg>::run()`
     * before entering the dispatch phase.
     *
     * @param task          Pointer to the crunch task (must not be nullptr).
     * @param watchdogKick  Optional platform watchdog kick callback.
     */
    void configure(ICrunchTask *task, WatchdogKickFn watchdogKick = nullptr)
    {
        m_task         = task;
        m_watchdogKick = watchdogKick;
    }

    // =====================================================================
    // Run Loop
    // =====================================================================

    /**
     * @brief Execute the tight crunch loop until exit.
     *
     * Blocks the calling core. Exits when:
     * - The kernel leaves an active state.
     * - Any registered stop condition fires.
     * - `System<Cfg>::isSafetyAborted()` becomes true.
     * - Consecutive overrun count reaches `CfgCrunchMaxOverruns<Cfg>::value`.
     *
     * @param coreId Zero-based core identifier for watchdog kicks.
     */
    void runLoop(std::size_t coreId)
    {
        using S = System<Cfg>;

        m_consecutiveOverruns = 0;
        m_totalOverruns       = 0;

        // Pre-loop safety abort check — handles abort signalled before the
        // crunch loop starts (e.g. by ControlTask on core 0 during init).
        if (S::isSafetyAborted())
        {
            m_task->onCrunchAbort();
            S::errorLogger().log(ErrorLogger::ErrorCode::SOFT_ABORT, S::timer().nowMicros(), 0.0f);
            return;
        }

        while (S::isActiveState(S::kernelState()) && !S::anyStopConditionFired())
        {
            // 1. Safety abort check (sub-nanosecond atomic read)
            if (S::isSafetyAborted())
            {
                m_task->onCrunchAbort();
                S::errorLogger().log(ErrorLogger::ErrorCode::SOFT_ABORT, S::timer().nowMicros(), 0.0f);
                return;
            }

            // 2. Watchdog kick
            SputterMicros now = S::timer().nowMicros();
            S::watchdog().kick(coreId, now);
            if (m_watchdogKick)
                m_watchdogKick();

            // 3. Dispatch crunch iteration with timing
            SputterMicros start = S::timer().nowMicros();
            m_task->crunch(now);
            SputterMicros elapsed = S::timer().nowMicros() - start;

            // 4. Overrun detection
            if (elapsed > m_task->maxIterationUs())
            {
                ++m_consecutiveOverruns;
                ++m_totalOverruns;
                S::errorLogger().log(ErrorLogger::ErrorCode::CRUNCH_OVERRUN, now, static_cast<float>(elapsed));

                if (CfgCrunchMaxOverruns<Cfg>::value > 0 && m_consecutiveOverruns >= CfgCrunchMaxOverruns<Cfg>::value)
                {
                    m_task->onCrunchAbort();
                    S::errorLogger().log(ErrorLogger::ErrorCode::HARD_FAULT, now,
                                         static_cast<float>(m_consecutiveOverruns));
                    return;
                }
            }
            else
            {
                m_consecutiveOverruns = 0; // Reset on clean iteration
            }
        }
    }

    // =====================================================================
    // Accessors
    // =====================================================================

    /**
     * @brief Total overrun count across the entire run.
     * @return Cumulative overrun count.
     */
    uint32_t totalOverruns() const { return m_totalOverruns; }

    /**
     * @brief Current consecutive overrun count.
     * @return Consecutive overruns since last clean iteration.
     */
    uint32_t consecutiveOverruns() const { return m_consecutiveOverruns; }

    /**
     * @brief Access the configured crunch task.
     * @return Pointer to the crunch task, or nullptr if not configured.
     */
    ICrunchTask *task() const { return m_task; }

  private:
    ICrunchTask   *m_task{nullptr};
    WatchdogKickFn m_watchdogKick{nullptr};
    uint32_t       m_consecutiveOverruns{0};
    uint32_t       m_totalOverruns{0};
};

} // namespace Kernel
} // namespace SputterOS

#endif // SPUTTEROS_KERNEL_CRUNCHDISPATCHER_H
