#ifndef SPUTTEROS_OSAL_WATCHDOGSYNC_H
#define SPUTTEROS_OSAL_WATCHDOGSYNC_H

/**
 * @file WatchdogSync.h
 * @brief Inter-core heartbeat monitor for detecting stalled tasks.
 *
 * Each participating core (or task) periodically calls `kick()` to update
 * its atomic heartbeat timestamp. Any other core can call `isStale()` to
 * check whether a peer has missed its heartbeat deadline — indicating a
 * hang, infinite loop, or deadlock.
 *
 * ### Motivation (Issue 5)
 * If Core 1 (CommsTask) locks up on a malformed packet, Core 0
 * (ControlTask) previously had no way to detect this. WatchdogSync
 * provides the detection mechanism: ControlTask checks
 * `isStale(kCommsCore, now, kHeartbeatTimeout)` each tick and triggers
 * a safe-state abort when the check fails.
 *
 * ### Usage
 * @code
 *   // Shared global (constructed before cores start)
 *   WatchdogSync<2> g_watchdog;
 *
 *   // Core 1 — inside CommsTask::tick()
 *   g_watchdog.kick(1, systemTimeMicros);
 *
 *   // Core 0 — inside ControlTask::tick() or DiagnosticsTask::tick()
 *   if (g_watchdog.isStale(1, systemTimeMicros, SputterMicros(200000)))
 *   {
 *       // Trigger safe-state abort
 *   }
 * @endcode
 *
 * @tparam N_CORES Number of monitored cores/tasks.
 *
 * @note All operations are lock-free (atomic load/store with
 *       acquire/release). Safe to call from any core or ISR.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */

#include "sputteros/osal/SputterTime.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace SputterOS
{

template <std::size_t N_CORES> class WatchdogSync
{
    static_assert(N_CORES >= 1, "WatchdogSync requires at least 1 monitored core.");

  public:
    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /**
     * @brief Construct a WatchdogSync with all heartbeats at time zero.
     *
     * All cores are considered "not yet kicked" until the first `kick()` call.
     * `isStale()` returns false for any core whose heartbeat is still zero,
     * so the startup path is safe without special initialization.
     */
    WatchdogSync()
    {
        for (std::size_t i = 0; i < N_CORES; ++i)
        {
            m_lastKick[i].store(0, std::memory_order_relaxed);
            m_alive[i].store(false, std::memory_order_relaxed);
        }
    }

    // -----------------------------------------------------------------------
    // Heartbeat API
    // -----------------------------------------------------------------------

    /**
     * @brief Update the heartbeat timestamp for the given core.
     *
     * Must be called periodically by the owning core/task.
     *
     * @param coreId     Zero-based core identifier. Must be < N_CORES.
     * @param systemTime Current monotonic time.
     */
    void kick(std::size_t coreId, SputterMicros systemTimeMicros)
    {
        m_lastKick[coreId].store(static_cast<int64_t>(systemTimeMicros), std::memory_order_release);
        m_alive[coreId].store(true, std::memory_order_release);
    }

    // -----------------------------------------------------------------------
    // Staleness check
    // -----------------------------------------------------------------------

    /**
     * @brief Check whether a peer core's heartbeat has exceeded the timeout.
     *
     * @param coreId     Zero-based core identifier to check.
     * @param systemTime Current monotonic time of the checking core.
     * @param timeout    Maximum allowed interval between kicks.
     * @return true if the core has been kicked at least once AND its last
     *         kick is older than `timeout` relative to `systemTime`.
     *         Returns false if the core has never kicked (not yet started).
     */
    bool isStale(std::size_t coreId, SputterMicros systemTimeMicros, SputterMicros timeout) const
    {
        // If the core has never kicked, it hasn't started yet — not stale.
        if (!m_alive[coreId].load(std::memory_order_acquire))
        {
            return false;
        }

        const auto lastKick = static_cast<SputterMicros>(m_lastKick[coreId].load(std::memory_order_acquire));
        return (systemTimeMicros - lastKick) > timeout;
    }

    // -----------------------------------------------------------------------
    // Queries
    // -----------------------------------------------------------------------

    /**
     * @brief Return the last-kick timestamp for a core.
     * @param coreId Zero-based core identifier.
     * @return Last recorded heartbeat time, or 0 ms if never kicked.
     */
    SputterMicros lastKickTime(std::size_t coreId) const
    {
        return static_cast<SputterMicros>(m_lastKick[coreId].load(std::memory_order_acquire));
    }

    /**
     * @brief Check whether a core has ever called kick().
     * @param coreId Zero-based core identifier.
     * @return true if kick() has been called at least once for this core.
     */
    bool hasStarted(std::size_t coreId) const { return m_alive[coreId].load(std::memory_order_acquire); }

    /**
     * @brief Return the compile-time core count.
     */
    static constexpr std::size_t coreCount() { return N_CORES; }

  private:
    std::atomic<int64_t> m_lastKick[N_CORES]; /**< @brief Heartbeat timestamps (µs, as count). */
    std::atomic<bool>    m_alive[N_CORES];    /**< @brief true once a core has called kick(). */
};

} // namespace SputterOS

#endif // SPUTTEROS_OSAL_WATCHDOGSYNC_H
