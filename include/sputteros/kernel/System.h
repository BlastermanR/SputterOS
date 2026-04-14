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
#include "sputteros/kernel/CoreDispatchMode.h"
#include "sputteros/kernel/CrunchDispatcher.h"
#include "sputteros/kernel/KernelState.h"
#include "sputteros/kernel/metrics/CoreUtilizationTracker.h"
#include "sputteros/kernel/metrics/SchedulerHealthMetrics.h"
#include "sputteros/kernel/tasks/BackgroundDiagnosticsTask.h"
#include "sputteros/kernel/tasks/ScheduledCommsTask.h"
#include "sputteros/kernel/tasks/ScheduledControlTask.h"
#include "sputteros/osal/sync/LockFreeQueue.h"
#include "sputteros/osal/sync/MultiCoreSync.h"
#include "sputteros/osal/sync/WatchdogSync.h"
#include "sputteros/osal/tasks/IBackgroundTask.h"
#include "sputteros/osal/tasks/ICrunchTask.h"
#include "sputteros/osal/tasks/ITask.h"
#include "sputteros/utils/MemoryProfiler.h"
#include "sputteros/utils/PerformanceSnapshot.h"
#include "sputteros/utils/QueueDepthMonitor.h"
#include "sputteros/utils/logging/ErrorLogger.h"
#include "sputteros/utils/logging/TelemetryLogger.h"

#include "sputteros/utils/PlatformAssert.h"

#include <chrono>
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
    static constexpr std::size_t kCoreCount         = Cfg::kCoreCount;
    static constexpr std::size_t kQueueCapacity     = Cfg::kQueueCapacity;
    static constexpr bool        kMultiCore         = (kCoreCount >= 2);
    static constexpr std::size_t kMaxStopConditions = 8;

    /**
     * @brief User-supplied predicate evaluated each tick by `run()`.
     *
     * Return `true` to signal the kernel to stop. Multiple stop conditions
     * are OR'd — any one returning `true` terminates the run loop.
     */
    using StopConditionFn = bool (*)();

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

        /** @brief Dispatch strategy for this core (default: FLAT_LOOP). */
        Kernel::CoreDispatchMode mode = Kernel::CoreDispatchMode::FLAT_LOOP;

        /** @brief Exclusive crunch task for CRUNCH mode (nullptr in FLAT_LOOP). */
        ICrunchTask *crunchTask = nullptr;

        /** @brief Whether this core has any tasks (scheduled or crunch). */
        bool isActive() const { return taskCount > 0 || crunchTask != nullptr; }

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
        SPUTTEROS_ASSERT(s_built && "Call SystemBuilder::build() before System::init()");
        if (coreId >= kCoreCount)
            return;

        // Transition to INITIALIZING on first core to call init()
        if (s_kernelState == Kernel::KernelState::CONFIGURED)
            transitionTo(Kernel::KernelState::INITIALIZING);

        for (std::size_t t = 0; t < s_cores[coreId].taskCount; ++t)
        {
            ITask *tsk = s_cores[coreId].tasks[t];
            if (tsk)
            {
                tsk->init();
            }
        }

        // Initialize crunch task on CRUNCH-mode cores
        if (s_cores[coreId].mode == Kernel::CoreDispatchMode::CRUNCH && s_cores[coreId].crunchTask)
        {
            s_cores[coreId].crunchTask->init();
        }

        // Initialize background tasks on the designated background core
        if (coreId == s_backgroundCoreId)
        {
            for (std::size_t b = 0; b < s_backgroundTaskCount; ++b)
            {
                if (s_backgroundTasks[b])
                {
                    s_backgroundTasks[b]->init();
                }
            }
        }

        // Transition to RUNNING after tasks initialized
        if (s_kernelState == Kernel::KernelState::INITIALIZING)
            transitionTo(Kernel::KernelState::RUNNING);

        s_coreInitialized[coreId] = true;
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
        SPUTTEROS_ASSERT(s_built && "Call SystemBuilder::build() before System::tick()");
        if (coreId >= kCoreCount)
            return;

        // Rollover detection: if current time is less than last recorded time,
        // the timer has wrapped. Log a fault for safety awareness.
        if (systemTimeMicros < s_lastTime[coreId] && s_lastTime[coreId] != 0)
        {
            s_errorLogger.log(ErrorLogger::ErrorCode::TIMER_ROLLOVER, systemTimeMicros, 0.0f);
        }
        s_lastTime[coreId] = systemTimeMicros;

        s_utilTracker[coreId].recordTickStart(systemTimeMicros);

        // Phase 1: Scheduled tasks
        SputterMicros busyAccum = 0;
        for (std::size_t t = 0; t < s_cores[coreId].taskCount; ++t)
        {
            ITask *tsk = s_cores[coreId].tasks[t];
            if (tsk)
            {
                tsk->timer().start();
                tsk->tick(systemTimeMicros);
                tsk->timer().stop();
                busyAccum += tsk->timer().lastDuration();
            }
        }

        // Phase 2: Background tasks — round-robin, budget-gated after first dispatch.
        // At least one background task always dispatches per tick (the round-robin
        // head) to ensure DiagnosticsTask can always monitor for budget violations.
        // Additional tasks are budget-capped by the remaining gap time.
        if (coreId == s_backgroundCoreId && s_backgroundTaskCount > 0)
        {
            SputterMicros gapBudget = CfgControlBudgetUs<Cfg>::value;
            SputterMicros used      = busyAccum;

            for (std::size_t i = 0; i < s_backgroundTaskCount; ++i)
            {
                IBackgroundTask *bg = s_backgroundTasks[s_bgRoundRobin];
                s_bgRoundRobin      = (s_bgRoundRobin + 1) % s_backgroundTaskCount;

                if (bg)
                {
                    // After the first dispatch, enforce budget cap
                    if (i > 0)
                    {
                        SputterMicros taskBudget = bg->maxBudgetUs();
                        if (used + taskBudget > gapBudget)
                            break;
                    }

                    bg->timer().start();
                    bg->tick(systemTimeMicros);
                    bg->timer().stop();
                    used += bg->timer().lastDuration();
                }
            }

            busyAccum = used;
        }

        SputterMicros tickEndTime = s_timer.nowMicros();
        s_utilTracker[coreId].recordTickEnd(tickEndTime, busyAccum);

        // Record gap time (wall time - busy time) for scheduler health
        SputterMicros wallTime = (tickEndTime >= systemTimeMicros) ? (tickEndTime - systemTimeMicros) : 0;
        SputterMicros gapUs    = (wallTime >= busyAccum) ? (wallTime - busyAccum) : 0;
        s_schedulerHealth.recordGap(gapUs, tickEndTime);

        // Sample queue depth once per tick (on core 0 to avoid double-counting)
        if (coreId == 0)
        {
            s_queueMonitor.sample(s_commandQueue.size(), tickEndTime);
        }
    }

    // =====================================================================
    // Blocking Run Loop
    // =====================================================================

    /**
     * @brief Execute the kernel run loop on a specific core until exit.
     *
     * Internalizes the entire runtime lifecycle for one core:
     *   1. Signals `MultiCoreSync::setInit(coreId)`.
     *   2. Calls `init(coreId)` if this core has not been initialized.
     *   3. Waits at `startupBarrier()` until all active cores are ready.
     *   4. Ticks in a busy-wait loop until any registered stop condition
     *      fires or the kernel leaves an active state.
     *   5. Transitions to SHUTTING_DOWN → SHUTDOWN (best-effort, first
     *      core to reach this point performs the transition).
     *   6. Waits at `shutdownBarrier()` for peer cores to finish.
     *
     * For single-core systems, barriers are no-ops.
     * For multi-core, each core must call `run()` from its own thread
     * — the kernel does not launch threads internally.
     *
     * @param coreId Zero-based core identifier (0 for main core).
     */
    static void run(std::size_t coreId)
    {
        SPUTTEROS_ASSERT(s_built && "Call SystemBuilder::build() before System::run()");
        if (coreId >= kCoreCount)
            return;

        // Phase 1: Multi-core init signaling
        s_sync.setInit(coreId);

        // Phase 2: Initialize tasks if not already done
        if (!s_coreInitialized[coreId])
        {
            init(coreId);
        }

        // Phase 3: Wait for all cores to be ready
        s_sync.startupBarrier(coreId, std::chrono::milliseconds{2000});

        // Phase 4: Dispatch based on core mode
        auto &core = s_cores[coreId];

        if (core.mode == Kernel::CoreDispatchMode::CRUNCH && core.crunchTask)
        {
            // CRUNCH mode: tight loop with watchdog, overrun, and abort.
            s_crunchDispatcher.configure(core.crunchTask, s_watchdogKickFn);
            s_crunchDispatcher.runLoop(coreId);
        }
        else
        {
            // FLAT_LOOP mode: existing tick-based dispatch
            while (isActiveState(s_kernelState) && !anyStopConditionFired())
            {
                tick(coreId, s_timer.nowMicros());
            }
        }

        // Phase 5: Kernel state shutdown (best-effort, first core wins)
        if (s_kernelState == Kernel::KernelState::RUNNING)
        {
            transitionTo(Kernel::KernelState::SHUTTING_DOWN);
        }
        if (s_kernelState == Kernel::KernelState::SHUTTING_DOWN)
        {
            transitionTo(Kernel::KernelState::SHUTDOWN);
        }

        // Phase 6: Multi-core shutdown barrier
        s_sync.shutdownBarrier(coreId, std::chrono::milliseconds{2000});
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
     * Wraps the injected `MicrosecondSource` with convenience
     * getters returning `double` in standard time units.
     * Set the clock source via
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
     * @brief Access the kernel-owned telemetry logger.
     *
     * Tasks and user code log human-readable status messages here.
     * `BackgroundDiagnosticsTask` drains this logger each tick if
     * a drain callback was registered via `SystemBuilder::setTelemetryDrain()`.
     *
     * @return Reference to the embedded `TelemetryLogger`.
     */
    static TelemetryLogger &telemetryLogger() { return s_telemetryLogger; }

    /**
     * @brief Access the kernel-owned memory profiler.
     * @return Reference to the embedded `MemoryProfiler`.
     */
    static MemoryProfiler &memProfiler() { return s_memProfiler; }

    /**
     * @brief Access the per-core utilization tracker.
     * @param coreId: Zero-based core identifier.
     * @return Reference to the tracker for the given core.
     */
    static Kernel::CoreUtilizationTracker &coreUtilization(std::size_t coreId)
    {
        SPUTTEROS_ASSERT(coreId < kCoreCount);
        return s_utilTracker[coreId];
    }

    /**
     * @brief Access the queue depth monitor.
     * @return Reference to the queue depth monitor.
     */
    static QueueDepthMonitor &queueMonitor() { return s_queueMonitor; }

    /**
     * @brief Access the scheduler health metrics.
     * @return Reference to the scheduler health aggregator.
     */
    static Kernel::SchedulerHealthMetrics &schedulerHealth() { return s_schedulerHealth; }

    /**
     * @brief Capture a point-in-time snapshot of all performance metrics.
     *
     * Aggregates per-core utilization, command queue depth, memory
     * usage, scheduler health, and per-task timing with histograms
     * into a single `PerformanceSnapshot` value.
     *
     * @warning The returned struct consumes ~1.2 KiB of stack (16-task,
     *          4-core configuration). Call from a top-level or background
     *          context, not from within a deeply nested control tick,
     *          to avoid stack overflow on constrained targets (e.g. RP2040).
     *
     * @return Snapshot value copy — safe to read from any context.
     */
    static PerformanceSnapshot snapshot()
    {
        PerformanceSnapshot snap{};
        snap.timestamp = s_timer.nowMicros();

        // Core utilization
        snap.coreCount = kCoreCount;
        for (std::size_t c = 0; c < kCoreCount && c < kMaxSnapshotCores; ++c)
        {
            snap.coreUtilization[c] = s_utilTracker[c].getUtilization();
        }

        // Command queue
        snap.queueDepth    = s_queueMonitor.lastDepth();
        snap.queueMaxDepth = s_queueMonitor.maxDepth();
        snap.queueAvgDepth = s_queueMonitor.averageDepth();

        // Memory
        snap.peakHeapUsed   = s_memProfiler.getPeakHeapUsedBytes();
        snap.freeHeap       = MemoryProfiler::getFreeHeapBytes();
        snap.stackHighWater = MemoryProfiler::getStackHighWaterMark();

        // Scheduler health
        snap.totalGapUs          = s_schedulerHealth.totalGapUs();
        snap.maxGapUs            = s_schedulerHealth.maxGapUs();
        snap.avgGapUs            = s_schedulerHealth.averageGapUs();
        snap.schedulerTickCount  = s_schedulerHealth.tickCount();
        snap.totalOverruns       = s_schedulerHealth.totalOverruns();
        snap.totalDeadlineMisses = s_schedulerHealth.totalDeadlineMisses();

        // Per-task details
        std::size_t idx = 0;
        for (std::size_t c = 0; c < kCoreCount && idx < kMaxSnapshotTasks; ++c)
        {
            for (std::size_t t = 0; t < s_cores[c].taskCount && idx < kMaxSnapshotTasks; ++t)
            {
                ITask *tsk = s_cores[c].tasks[t];
                if (!tsk)
                    continue;
                TaskSnapshot &ts     = snap.tasks[idx];
                ts.taskIndex         = t;
                ts.coreId            = c;
                const auto &tmr      = tsk->timer();
                ts.lastUs            = tmr.lastDuration();
                ts.minUs             = tmr.minDuration();
                ts.maxUs             = tmr.maxDuration();
                ts.avgUs             = tmr.getAverageDurationUs();
                ts.samples           = tmr.sampleCount();
                ts.overruns          = tmr.overrunCount();
                ts.misses            = tmr.deadlineMissCount();
                const uint32_t *hist = tmr.histogram();
                for (std::size_t b = 0; b < Kernel::TaskTimer::kHistogramBuckets; ++b)
                {
                    ts.histogram[b] = hist[b];
                }
                ++idx;
            }
        }
        snap.taskCount = idx;

        return snap;
    }

    /**
     * @brief Access the registered background tasks.
     * @return Pointer to the background task array.
     */
    static IBackgroundTask *const *backgroundTasks() { return s_backgroundTasks; }

    /**
     * @brief Number of registered background tasks.
     * @return Count of background tasks.
     */
    static std::size_t backgroundTaskCount() { return s_backgroundTaskCount; }

    // =====================================================================
    // Queries
    // =====================================================================

    /**
     * @brief Query whether `SystemBuilder::build()` has been called.
     * @return true after a successful `build()` call.
     */
    static bool isBuilt() { return s_built; }

    /**
     * @brief Read-only accessor for the current kernel lifecycle state.
     * @return Current `KernelState` value.
     */
    static Kernel::KernelState kernelState() { return s_kernelState; }

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

    // =====================================================================
    // Safety Abort Bridge
    // =====================================================================

    /**
     * @brief Signal a safety abort from ControlTask to all cores.
     *
     * Called by `ScheduledControlTask::evaluateSafety()` when any
     * `ISafetyMonitor::isSafe()` returns false. The flag is checked
     * by `CrunchDispatcher` every iteration via `isSafetyAborted()`.
     *
     * Uses `memory_order_release` to ensure all preceding writes
     * (e.g. fault log entries) are visible to the reading core.
     */
    static void signalSafetyAbort() { s_safetyAbort.store(true, std::memory_order_release); }

    /**
     * @brief Check whether a safety abort has been signalled.
     *
     * Called by `CrunchDispatcher` every iteration. Uses
     * `memory_order_acquire` to synchronise with the producer's
     * `memory_order_release` store in `signalSafetyAbort()`.
     *
     * @return true if the abort flag is set.
     */
    static bool isSafetyAborted() { return s_safetyAbort.load(std::memory_order_acquire); }

    /**
     * @brief Clear the safety abort flag.
     *
     * Called during recovery or reset. Uses `memory_order_relaxed`
     * because clearing is only done when no concurrent reader is
     * expected (post-shutdown or test teardown).
     */
    static void clearSafetyAbort() { s_safetyAbort.store(false, std::memory_order_relaxed); }

  private:
    friend class SystemBuilder<Cfg>;
    friend class Kernel::CrunchDispatcher<Cfg>;
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
        s_allTaskCount        = 0;
        s_backgroundTaskCount = 0;
        s_bgRoundRobin        = 0;
        s_backgroundCoreId    = 0;
        for (auto &t : s_backgroundTasks)
            t = nullptr;
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
        for (std::size_t c = 0; c < kCoreCount; ++c)
        {
            s_utilTracker[c].reset();
        }
        s_queueMonitor.reset();
        s_schedulerHealth.reset();
        s_kernelState = Kernel::KernelState::UNCONFIGURED;

        // Reset run() API state
        s_telemetryLogger.clear();
        s_stopConditionCount = 0;
        for (auto &fn : s_stopConditions)
            fn = nullptr;
        s_drainFn  = nullptr;
        s_drainCtx = nullptr;
        for (auto &init : s_coreInitialized)
            init = false;
        s_safetyAbort.store(false, std::memory_order_relaxed);
        s_watchdogKickFn = nullptr;
    }

    // =====================================================================
    // Run-Loop Helpers (private)
    // =====================================================================

    /**
     * @brief Check whether the kernel is in a state where ticking is valid.
     * @param state Current kernel lifecycle state.
     * @return true for RUNNING, SUSPENDING, or SUSPENDED.
     */
    static bool isActiveState(Kernel::KernelState state)
    {
        return state == Kernel::KernelState::RUNNING || state == Kernel::KernelState::SUSPENDING ||
               state == Kernel::KernelState::SUSPENDED;
    }

    /**
     * @brief Evaluate all registered stop condition predicates.
     * @return true if any stop condition returned true.
     */
    static bool anyStopConditionFired()
    {
        for (std::size_t i = 0; i < s_stopConditionCount; ++i)
        {
            if (s_stopConditions[i] && s_stopConditions[i]())
                return true;
        }
        return false;
    }

    // =====================================================================
    // Static Infrastructure (no heap)
    // =====================================================================

    static constexpr std::size_t kMaxTotalTasks = kCoreCount * kMaxTasksPerCore;

    inline static LockFreeQueue<Cfg, kQueueCapacity> s_commandQueue{};
    inline static WatchdogSync<kCoreCount>           s_watchdog{};
    inline static SyncType                           s_sync{};
    inline static ErrorLogger                        s_errorLogger{};
    inline static TelemetryLogger                    s_telemetryLogger{};
    inline static MemoryProfiler                     s_memProfiler{};
    inline static SystemTimer                        s_timer{};
    inline static Kernel::CoreUtilizationTracker     s_utilTracker[kCoreCount]{};
    inline static QueueDepthMonitor                  s_queueMonitor{};
    inline static Kernel::SchedulerHealthMetrics     s_schedulerHealth{};

    // =====================================================================
    // CrunchDispatcher (CRUNCH-mode cores)
    // =====================================================================

    inline static Kernel::CrunchDispatcher<Cfg> s_crunchDispatcher{};

    // =====================================================================
    // Kernel Tasks (emplaced by SystemBuilder::build())
    // =====================================================================

    inline static std::optional<Kernel::ScheduledControlTask<Cfg>> s_controlTask{};
    inline static std::optional<Kernel::ScheduledCommsTask<Cfg>>   s_commsTask{};
    inline static std::optional<Kernel::BackgroundDiagnosticsTask> s_diagsTask{};

    // =====================================================================
    // Background Task Ring
    // =====================================================================

    inline static IBackgroundTask *s_backgroundTasks[CfgMaxBackgroundTasks<Cfg>::value] = {};
    inline static std::size_t      s_backgroundTaskCount{0};
    inline static std::size_t      s_bgRoundRobin{0};
    inline static std::size_t      s_backgroundCoreId{0};

    // =====================================================================
    // Per-Core Task Lists
    // =====================================================================

    inline static CoreData    s_cores[kCoreCount]{};
    inline static ITask      *s_allTasks[kMaxTotalTasks]{};
    inline static std::size_t s_allTaskCount{0};

    // =====================================================================
    // State
    // =====================================================================

    inline static bool                s_built{false};
    inline static SputterMicros       s_lastTime[kCoreCount]{};
    inline static Kernel::KernelState s_kernelState{Kernel::KernelState::UNCONFIGURED};
    inline static std::atomic<bool>   s_safetyAbort{false};

    // =====================================================================
    // Run API State
    // =====================================================================

    inline static StopConditionFn               s_stopConditions[kMaxStopConditions]{};
    inline static std::size_t                   s_stopConditionCount{0};
    inline static TelemetryLogger::DrainWriteFn s_drainFn{nullptr};
    inline static void                         *s_drainCtx{nullptr};
    inline static bool                          s_coreInitialized[kCoreCount]{};

    /** @brief Platform watchdog kick stored during build for CrunchDispatcher. */
    using WatchdogKickFn = void (*)();
    inline static WatchdogKickFn s_watchdogKickFn{nullptr};

    // =====================================================================
    // Kernel State Machine
    // =====================================================================

    /**
     * @brief Attempt a kernel lifecycle state transition.
     *
     * Validates the transition against the allowed transition table
     * (see docs/SchedulingDesign.md §7). Invalid
     * transitions are logged to `ErrorLogger` and rejected.
     *
     * @param target Desired next state.
     * @return true if the transition was valid and applied.
     */
    static bool transitionTo(Kernel::KernelState target)
    {
        using KS   = Kernel::KernelState;
        bool valid = false;
        switch (s_kernelState)
        {
        case KS::UNCONFIGURED:
            valid = (target == KS::CONFIGURED);
            break;
        case KS::CONFIGURED:
            valid = (target == KS::INITIALIZING);
            break;
        case KS::INITIALIZING:
            valid = (target == KS::RUNNING);
            break;
        case KS::RUNNING:
            valid = (target == KS::SUSPENDING || target == KS::ABORTING || target == KS::SHUTTING_DOWN);
            break;
        case KS::SUSPENDING:
            valid = (target == KS::SUSPENDED);
            break;
        case KS::SUSPENDED:
            valid = (target == KS::RUNNING || target == KS::SHUTTING_DOWN);
            break;
        case KS::ABORTING:
            valid = (target == KS::ABORTED);
            break;
        case KS::ABORTED:
            valid = (target == KS::RUNNING || target == KS::SHUTTING_DOWN);
            break;
        case KS::SHUTTING_DOWN:
            valid = (target == KS::SHUTDOWN);
            break;
        case KS::SHUTDOWN:
            valid = false;
            break;
        }
        if (valid)
        {
            s_kernelState = target;
            return true;
        }
        s_errorLogger.log(ErrorLogger::ErrorCode::INVALID_STATE, SputterMicros(0),
                          static_cast<float>(static_cast<uint8_t>(target)));
        return false;
    }
};

} // namespace SputterOS

#endif // SPUTTEROS_KERNEL_SYSTEM_H
