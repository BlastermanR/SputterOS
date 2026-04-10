#ifndef SPUTTEROS_KERNEL_SYSTEM_H
#define SPUTTEROS_KERNEL_SYSTEM_H

/**
 * @file System.h
 * @brief Runtime kernel singleton for SputterOS.
 *
 * `System<Cfg>` owns all shared kernel infrastructure - the lock-free
 * command queue, watchdog, multi-core synchronization, diagnostics
 * utilities, and the three kernel tasks — as static data members.
 * It exposes the runtime API (`init`, `tick`, accessors) through
 * static member functions so that user code never needs to hold a
 * reference to a specific object.
 *
 * `SystemBuilder<Cfg>` is the only path for populating and validating
 * the `System`. After a successful `build()`, the user drives the
 * kernel entirely through `System<Cfg>` static calls:
 *
 * @code
 *   // Configuration phase — SystemBuilder fluent API
 *   SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
 *   builder.setStream(&stream);
 *   builder.setWatchdogKick(kickFn);
 *   builder.core(0).addTask(&customTask);
 *   auto result = builder.build();
 *   if (!result) { return 1; }
 *
 *   // Runtime phase — System static API
 *   System<Cfg>::init(0);
 *   while (true) { System<Cfg>::tick(0, getTimeMicros()); }
 * @endcode
 *
 * @note `System<Cfg>` is a class template - each unique `Cfg` gets its
 *       own independent set of static data. In practice there is exactly
 *       one `Cfg` per binary, making this a true singleton.
 * @note Accessors like `errorLogger()` and `commandQueue()` are available
 *       before `build()`. Runtime methods (`init`, `tick`) require a
 *       prior successful `build()`.
 *
 * @tparam Cfg Configuration struct satisfying `ConfigValidator<Cfg>`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */

#include "sputteros/ConfigTraits.h"
#include "sputteros/kernel/CommsTask.h"
#include "sputteros/kernel/ControlTask.h"
#include "sputteros/kernel/DiagnosticsTask.h"
#include "sputteros/osal/sync/LockFreeQueue.h"
#include "sputteros/osal/sync/MultiCoreSync.h"
#include "sputteros/osal/sync/WatchdogSync.h"
#include "sputteros/osal/tasks/ITask.h"
#include "sputteros/utils/MemoryProfiler.h"
#include "sputteros/utils/logging/ErrorLogger.h"

#include <cassert>
#include <cstddef>
#include <optional>
#include <type_traits>

namespace SputterOS
{

// =========================================================================
// NoOpMultiCoreSync — stub for single-core configurations
// =========================================================================

/**
 * @brief No-op placeholder used when `Cfg::kCoreCount < 2`.
 *
 * Satisfies the same API surface as `MultiCoreSync<N>` so that
 * `System` can compile without template specialization.
 */
struct NoOpMultiCoreSync
{
    void setInit(std::size_t) {}
    void setReady(std::size_t) {}
    void setError(std::size_t, const char *) {}
    void setShutdown(std::size_t) {}

    bool startupBarrier(std::size_t, std::chrono::milliseconds,
                        std::chrono::milliseconds = std::chrono::milliseconds{1})
    {
        return true;
    }

    bool shutdownBarrier(std::size_t, std::chrono::milliseconds,
                         std::chrono::milliseconds = std::chrono::milliseconds{1})
    {
        return true;
    }
};

// Forward declarations
template <typename Cfg> class SystemBuilder;

namespace Kernel
{
struct KernelTestAccess;
} // namespace Kernel

// =========================================================================
// System
// =========================================================================

/**
 * @brief Runtime kernel singleton for SputterOS.
 *
 * Owns all shared infrastructure as `inline static` data members.
 * Populated by `SystemBuilder<Cfg>::build()`. Provides static
 * member functions for runtime operation (`init`, `tick`) and
 * accessor queries.
 *
 * @tparam Cfg Configuration struct satisfying `ConfigValidator<Cfg>`.
 */
template <typename Cfg> class System
{
    static_assert(ConfigValidator<Cfg>::value, "Invalid SputterOS configuration.");

  public:
    static constexpr std::size_t kCoreCount     = Cfg::kCoreCount;
    static constexpr std::size_t kQueueCapacity = Cfg::kQueueCapacity;
    static constexpr bool        kMultiCore     = (kCoreCount >= 2);

    /**
     * @brief Conditional MultiCoreSync type.
     */
    using SyncType = std::conditional_t<kMultiCore, MultiCoreSync<kCoreCount>, NoOpMultiCoreSync>;

    // =====================================================================
    // Per-Core Task Storage
    // =====================================================================

    /**
     * @brief Lightweight per-core task list.
     *
     * Stores an array of `ITask*` and a count. Populated by
     * `SystemBuilder` during configuration and `build()`.
     */
    struct CoreData
    {
        static constexpr std::size_t kMaxTasks = kMaxTasksPerCore;

        ITask      *tasks[kMaxTasks] = {};
        std::size_t taskCount        = 0;

        /** @brief Whether this core has any tasks. */
        bool isActive() const { return taskCount > 0; }

        /** @brief Get a task pointer by index. */
        ITask *task(std::size_t idx) const { return (idx < taskCount) ? tasks[idx] : nullptr; }

        /**
         * @brief Append a task to the back of this core's task list.
         */
        void addTask(ITask *tsk)
        {
            if (tsk && taskCount < kMaxTasks)
            {
                tasks[taskCount++] = tsk;
            }
        }

        /**
         * @brief Insert a task at the front, shifting others right.
         *
         * Used by `SystemBuilder::build()` to prepend kernel tasks
         * so they tick before user tasks (safety-first ordering).
         */
        void prependTask(ITask *tsk)
        {
            if (tsk && taskCount < kMaxTasks)
            {
                for (std::size_t i = taskCount; i > 0; --i)
                {
                    tasks[i] = tasks[i - 1];
                }
                tasks[0] = tsk;
                ++taskCount;
            }
        }
    };

    // =====================================================================
    // Runtime API (post-build, static)
    // =====================================================================

    /**
     * @brief Initialize all tasks on a specific core.
     *
     * Calls `ITask::init()` on every task registered to `coreId`,
     * in registration order.
     *
     * @param coreId: Zero-based core identifier.
     */
    static void init(std::size_t coreId)
    {
        assert(s_built && "Call SystemBuilder::build() before System::init()");
        if (coreId >= kCoreCount)
            return;
        for (std::size_t t = 0; t < s_cores[coreId].taskCount; ++t)
        {
            ITask *tsk = s_cores[coreId].tasks[t];
            if (tsk)
            {
                tsk->init();
            }
        }
    }

    /**
     * @brief Tick all tasks on a specific core once.
     *
     * Instruments each task with `timer().start()` / `timer().stop()`
     * so that `DiagnosticsTask` can monitor per-task execution time.
     *
     * Detects timer rollover (unsigned wrap — should never occur with
     * 64-bit µs, but guards against buggy clock sources) and logs a
     * `TIMER_ROLLOVER` fault to the `ErrorLogger`.
     *
     * @param coreId:           Zero-based core identifier.
     * @param systemTimeMicros: Current monotonic system time in microseconds.
     */
    static void tick(std::size_t coreId, SputterMicros systemTimeMicros)
    {
        assert(s_built && "Call SystemBuilder::build() before System::tick()");
        if (coreId >= kCoreCount)
            return;

        // Rollover detection: if current time is less than last recorded time,
        // the timer has wrapped. Log a fault for safety awareness.
        if (systemTimeMicros < s_lastTime[coreId] && s_lastTime[coreId] != 0)
        {
            s_errorLogger.log(ErrorLogger::ErrorCode::TIMER_ROLLOVER, systemTimeMicros, 0.0f);
        }
        s_lastTime[coreId] = systemTimeMicros;

        for (std::size_t t = 0; t < s_cores[coreId].taskCount; ++t)
        {
            ITask *tsk = s_cores[coreId].tasks[t];
            if (tsk)
            {
                tsk->timer().start();
                tsk->tick(systemTimeMicros);
                tsk->timer().stop();
            }
        }
    }

    // =====================================================================
    // Accessors (available before and after build)
    // =====================================================================

    /**
     * @brief Access the lock-free command queue.
     *
     * Available before `build()` so user tasks can wire to the queue
     * during construction.
     *
     * @return Reference to the SPSC command queue.
     */
    static LockFreeQueue<Cfg, kQueueCapacity> &commandQueue() { return s_commandQueue; }

    /**
     * @brief Access the inter-core watchdog.
     * @return Reference to the watchdog heartbeat monitor.
     */
    static WatchdogSync<kCoreCount> &watchdog() { return s_watchdog; }

    /**
     * @brief Access the multi-core synchronization barrier.
     * @return Reference to the sync barrier (or no-op stub).
     */
    static SyncType &multiCoreSync() { return s_sync; }

    /**
     * @brief Access the kernel-owned system timer.
     *
     * Wraps the injected `MicrosecondSource` with nholthaus/units
     * convenience getters. Set the clock source via
     * `SystemBuilder::setClockSource()` before calling `build()`.
     *
     * @return Reference to the `SystemTimer`.
     */
    static SystemTimer &timer() { return s_timer; }

    /**
     * @brief Access the kernel-owned error logger.
     *
     * Users may log application-level events via this reference.
     * The kernel's `DiagnosticsTask` also writes to this logger.
     *
     * @return Reference to the embedded `ErrorLogger`.
     */
    static ErrorLogger &errorLogger() { return s_errorLogger; }

    /**
     * @brief Access the kernel-owned memory profiler.
     * @return Reference to the embedded `MemoryProfiler`.
     */
    static MemoryProfiler &memProfiler() { return s_memProfiler; }

    // =====================================================================
    // Queries
    // =====================================================================

    /**
     * @brief Query whether `SystemBuilder::build()` has been called.
     * @return true after a successful `build()` call.
     */
    static bool isBuilt() { return s_built; }

    /**
     * @brief Number of tasks registered on a core.
     * @param coreId: Zero-based core identifier.
     */
    static std::size_t taskCount(std::size_t coreId) { return (coreId < kCoreCount) ? s_cores[coreId].taskCount : 0; }

    /**
     * @brief Get a task pointer for a given core and index.
     * @param coreId:  Zero-based core identifier.
     * @param taskIdx: Zero-based task index within the core.
     * @return Task pointer, or nullptr if out of range.
     */
    static ITask *task(std::size_t coreId, std::size_t taskIdx)
    {
        return (coreId < kCoreCount) ? s_cores[coreId].task(taskIdx) : nullptr;
    }

  private:
    friend class SystemBuilder<Cfg>;
    friend struct Kernel::KernelTestAccess;

    // =====================================================================
    // Test-Only Reset (friend access via KernelTestAccess)
    // =====================================================================

    /**
     * @brief Reset all static state to allow re-use in test fixtures.
     *
     * Clears task lists, destroys kernel tasks, resets built flag.
     * Only accessible via `KernelTestAccess`.
     */
    static void reset()
    {
        s_built = false;
        s_controlTask.reset();
        s_commsTask.reset();
        s_diagsTask.reset();
        s_errorLogger.clear();
        s_commandQueue.clear();
        s_allTaskCount = 0;
        for (std::size_t c = 0; c < kCoreCount; ++c)
        {
            s_cores[c]    = CoreData{};
            s_lastTime[c] = 0;
        }
        for (std::size_t t = 0; t < kMaxTotalTasks; ++t)
        {
            s_allTasks[t] = nullptr;
        }
        // Reinitialize sync primitives (atomics are not assignable)
        new (&s_watchdog) WatchdogSync<kCoreCount>{};
        new (&s_sync) SyncType{};
        s_timer = SystemTimer{};
    }

    // =====================================================================
    // Static Infrastructure (no heap)
    // =====================================================================

    static constexpr std::size_t kMaxTotalTasks = kCoreCount * kMaxTasksPerCore;

    inline static LockFreeQueue<Cfg, kQueueCapacity> s_commandQueue{};
    inline static WatchdogSync<kCoreCount>           s_watchdog{};
    inline static SyncType                           s_sync{};
    inline static ErrorLogger                        s_errorLogger{};
    inline static MemoryProfiler                     s_memProfiler{};
    inline static SystemTimer                        s_timer{};

    // =====================================================================
    // Kernel Tasks (emplaced by SystemBuilder::build())
    // =====================================================================

    inline static std::optional<Kernel::ControlTask<Cfg>> s_controlTask{};
    inline static std::optional<Kernel::CommsTask<Cfg>>   s_commsTask{};
    inline static std::optional<Kernel::DiagnosticsTask>  s_diagsTask{};

    // =====================================================================
    // Per-Core Task Lists
    // =====================================================================

    inline static CoreData    s_cores[kCoreCount]{};
    inline static ITask      *s_allTasks[kMaxTotalTasks]{};
    inline static std::size_t s_allTaskCount{0};

    // =====================================================================
    // State
    // =====================================================================

    inline static bool          s_built{false};
    inline static SputterMicros s_lastTime[kCoreCount]{};
};

} // namespace SputterOS

#endif // SPUTTEROS_KERNEL_SYSTEM_H
