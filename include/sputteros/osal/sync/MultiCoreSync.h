/**
 * @file MultiCoreSync.h
 * @brief Parameterizable multi-core synchronization barrier for embedded systems.
 *
 * Provides structured startup and shutdown barriers for N-core bare-metal
 * systems. Each core progresses through explicit lifecycle states and waits
 * at barriers until all peers arrive or a timeout elapses.
 *
 * ### Lifecycle States
 *
 * ```
 *  UNBORN → INIT → READY ──────────────────────────────► SHUTDOWN
 *                    │                                       ▲
 *                    └──► ... (running) ... ─► setShutdown()─┘
 *                    │
 *                    └──► ERROR  (any time, including barrier timeout)
 * ```
 *
 * - **UNBORN**: Default state before the core has started.
 * - **INIT**: Core has started local init but is not yet ready to run.
 * - **READY**: Core has passed all pre-run checks (validation, init).
 * - **ERROR**: Core encountered an unrecoverable fault.
 * - **SHUTDOWN**: Core is requesting an orderly exit.
 *
 * ### Typical Usage (2-core system)
 *
 * ```cpp
 * // main() on Core 0
 * g_sync.setInit(0);
 * controlTask.init();
 * if (!controlTask.validateDependencies())
 *     return g_sync.setError(0, "ControlTask validation failed"), 1;
 * if (!g_sync.startupBarrier(0, 5000))
 *     return 1;   // partner timed out — error already set
 *
 * // core1_entry() on Core 1
 * g_sync.setInit(1);
 * commsTask.init();
 * if (!commsTask.validateDependencies())
 *     return g_sync.setError(1, "CommsTask validation failed");
 * if (!g_sync.startupBarrier(1, 5000))
 *     return;
 * ```
 *
 * @tparam N_CORES Number of cores — fixed at compile time.
 *
 * @note All state transitions are atomic with acquire/release semantics.
 *       No dynamic allocation; no virtual dispatch; safe on Cortex-M33.
 * @note The polling loop inside barriers calls `yieldHook()`, which defaults
 *       to a no-op. Override via the constructor argument for bare-metal
 *       tight-loop mitigation (e.g., calling `tight_loop_contents()`).
 *
 * @note All timeout / interval parameters use `std::chrono::milliseconds`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */

#ifndef SPUTTEROS_OSAL_MULTICORE_SYNC_H
#define SPUTTEROS_OSAL_MULTICORE_SYNC_H

#include "sputteros/osal/sync/ICoreErrorHandler.h"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace SputterOS
{

// =============================================================================
// Core lifecycle state
// =============================================================================

/**
 * @brief Lifecycle state for a single core.
 *
 * States advance monotonically (with the exception of ERROR, which can
 * be entered from any state). SHUTDOWN is entered after READY once the
 * core has decided to exit.
 */
enum class CoreState : uint8_t
{
    UNBORN   = 0, /**< Core has not started yet (default). */
    INIT     = 1, /**< Core is performing local initialisation. */
    READY    = 2, /**< Core has completed init and is ready to run. */
    ERROR    = 3, /**< Core encountered an unrecoverable fault. */
    SHUTDOWN = 4, /**< Core is requesting an orderly exit. */
};

// =============================================================================
// MultiCoreSync
// =============================================================================

/**
 * @brief Parameterizable multi-core startup/shutdown barrier.
 *
 * @tparam N_CORES Number of cores in the system (minimum 2).
 */
template <std::size_t N_CORES> class MultiCoreSync
{
    static_assert(N_CORES >= 2, "MultiCoreSync requires at least 2 cores.");

  public:
    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------

    /**
     * @brief Construct a MultiCoreSync instance.
     * @param errorHandler  Optional callback invoked when any core enters ERROR.
     *                      May be nullptr (no callback).
     */
    explicit MultiCoreSync(ICoreErrorHandler *errorHandler = nullptr) : m_errorHandler(errorHandler)
    {
        for (std::size_t i = 0; i < N_CORES; ++i)
        {
            m_states[i].store(CoreState::UNBORN, std::memory_order_relaxed);
        }
    }

    // -------------------------------------------------------------------------
    // State transitions
    // -------------------------------------------------------------------------

    /**
     * @brief Advance the given core to INIT state.
     * @param coreId  Zero-based core identifier. Must be < N_CORES.
     */
    void setInit(std::size_t coreId) { m_states[coreId].store(CoreState::INIT, std::memory_order_release); }

    /**
     * @brief Advance the given core to READY state.
     * @param coreId  Zero-based core identifier.
     *
     * @note Prefer using startupBarrier() which calls this internally.
     *       Call directly only when a manual READY signal is needed.
     */
    void setReady(std::size_t coreId) { m_states[coreId].store(CoreState::READY, std::memory_order_release); }

    /**
     * @brief Place the given core into ERROR state and notify the error handler.
     * @param coreId  Zero-based core identifier.
     * @param reason  Human-readable description of the error (static string).
     */
    void setError(std::size_t coreId, const char *reason)
    {
        m_states[coreId].store(CoreState::ERROR, std::memory_order_release);
        if (m_errorHandler)
        {
            m_errorHandler->onCoreError(coreId, reason);
        }
    }

    /**
     * @brief Place the given core into SHUTDOWN state.
     * @param coreId  Zero-based core identifier.
     */
    void setShutdown(std::size_t coreId) { m_states[coreId].store(CoreState::SHUTDOWN, std::memory_order_release); }

    // -------------------------------------------------------------------------
    // Barriers
    // -------------------------------------------------------------------------

    /**
     * @brief Startup barrier — signals READY then waits until all cores are READY
     *        or any core enters ERROR.
     *
     * This is the primary synchronization point for multi-core startup. Each
     * core calls this once after completing local initialization. The call
     * blocks until every peer has reached READY, the timeout elapses, or any
     * core reports an error.
     *
     * On timeout the calling core's state is set to ERROR automatically and
     * the error handler is invoked.
     *
     * @param coreId       Zero-based ID of the calling core.
     * @param timeout      Maximum wait time.
     * @param pollInterval How often to re-check peer states (default 1 ms).
     * @return true  if all cores reached READY within the timeout.
     * @return false if the barrier timed out or any core is in ERROR.
     */
    bool startupBarrier(std::size_t coreId, std::chrono::milliseconds timeout,
                        std::chrono::milliseconds pollInterval = std::chrono::milliseconds{1})
    {
        setReady(coreId);

        auto elapsed = std::chrono::milliseconds{0};
        while (elapsed < timeout)
        {
            if (anyError())
            {
                setError(coreId, "Peer entered ERROR during startup barrier");
                return false;
            }
            if (allInState(CoreState::READY))
            {
                return true;
            }
            pollDelay(pollInterval);
            elapsed += pollInterval;
        }

        setError(coreId, "Startup barrier timeout");
        return false;
    }

    /**
     * @brief Shutdown barrier — signals SHUTDOWN then waits until all cores
     *        are in SHUTDOWN or ERROR.
     *
     * Call once per core when the main loop exits normally. Ensures both cores
     * finish outstanding work before any shared resources are torn down.
     *
     * @param coreId       Zero-based ID of the calling core.
     * @param timeout      Maximum wait time.
     * @param pollInterval Polling interval (default 1 ms).
     * @return true  if all cores reached SHUTDOWN/ERROR within timeout.
     * @return false if the barrier timed out.
     */
    bool shutdownBarrier(std::size_t coreId, std::chrono::milliseconds timeout,
                         std::chrono::milliseconds pollInterval = std::chrono::milliseconds{1})
    {
        setShutdown(coreId);

        auto elapsed = std::chrono::milliseconds{0};
        while (elapsed < timeout)
        {
            if (allTerminated())
            {
                return true;
            }
            pollDelay(pollInterval);
            elapsed += pollInterval;
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // Ordered wait
    // -------------------------------------------------------------------------

    /**
     * @brief Block until a specific peer core reaches at least the given state.
     * @param peerId       Zero-based ID of the core to wait on.
     * @param targetState  State to wait for.
     * @param timeout      Maximum wait time.
     * @param pollInterval Polling interval (default 1 ms).
     * @return true  if the peer reached (or exceeded) targetState.
     * @return false if the timeout elapsed first.
     *
     * Useful for ordered initialization: Core 1 can wait for Core 0 to reach
     * READY before touching shared objects initialized by Core 0.
     */
    bool waitForCore(std::size_t peerId, CoreState targetState, std::chrono::milliseconds timeout,
                     std::chrono::milliseconds pollInterval = std::chrono::milliseconds{1})
    {
        auto elapsed = std::chrono::milliseconds{0};
        while (elapsed < timeout)
        {
            CoreState current = m_states[peerId].load(std::memory_order_acquire);
            if (static_cast<uint8_t>(current) >= static_cast<uint8_t>(targetState))
            {
                return true;
            }
            pollDelay(pollInterval);
            elapsed += pollInterval;
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // Queries
    // -------------------------------------------------------------------------

    /**
     * @brief Return the current lifecycle state of a core.
     * @param coreId  Zero-based core identifier.
     * @return Current CoreState of the requested core.
     */
    CoreState coreState(std::size_t coreId) const { return m_states[coreId].load(std::memory_order_acquire); }

    /**
     * @brief Return true if any core is in ERROR state.
     */
    bool anyError() const
    {
        for (std::size_t i = 0; i < N_CORES; ++i)
        {
            if (m_states[i].load(std::memory_order_acquire) == CoreState::ERROR)
            {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Return true if all cores are in READY state.
     */
    bool allReady() const { return allInState(CoreState::READY); }

    /**
     * @brief Return true if all cores are in SHUTDOWN or ERROR state.
     */
    bool allTerminated() const
    {
        for (std::size_t i = 0; i < N_CORES; ++i)
        {
            CoreState s = m_states[i].load(std::memory_order_acquire);
            if (s != CoreState::SHUTDOWN && s != CoreState::ERROR)
            {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Return the compile-time core count.
     */
    static constexpr std::size_t coreCount() { return N_CORES; }

  private:
    std::atomic<CoreState> m_states[N_CORES];
    ICoreErrorHandler     *m_errorHandler;

    bool allInState(CoreState target) const
    {
        for (std::size_t i = 0; i < N_CORES; ++i)
        {
            if (m_states[i].load(std::memory_order_acquire) != target)
            {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Delay hook called each poll iteration inside barriers.
     *
     * The default implementation busy-waits by counting cycles. The parent
     * application should not need to override this — the portable millisecond
     * granularity is sufficient for barrier polling.
     *
     * @note On Pico/RP2350 the Pico SDK `tight_loop_contents()` intrinsic is
     *       called by each core's own loop (in main.cpp / Core1Main.cpp) —
     *       this delay hook does not need to replicate it.
     */
    static void pollDelay(std::chrono::milliseconds ms)
    {
        /* Portable busy-wait: ~1 ms at typical Cortex-M33 frequencies.
         * Replace with a HAL sleep if a timer is available at this stage. */
        volatile uint32_t cycles = static_cast<uint32_t>(ms.count()) * 125000U; // ~125 MHz default clock
        while (cycles--)
        {
        }
    }
};

} // namespace SputterOS

#endif // SPUTTEROS_OSAL_MULTICORE_SYNC_H
