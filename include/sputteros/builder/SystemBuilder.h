#ifndef SPUTTEROS_BUILDER_SYSTEM_BUILDER_H
#define SPUTTEROS_BUILDER_SYSTEM_BUILDER_H

/**
 * @file SystemBuilder.h
 * @brief Configuration and validation builder for SputterOS.
 *
 * Provides a fluent Builder Pattern API that lets developers declare
 * the system topology — cores, tasks, and kernel dependencies — then
 * call `build()` to validate the configuration and populate the
 * `System<Cfg>` runtime singleton.
 *
 * After a successful `build()`, use the `System<Cfg>` static API for
 * runtime operations (`init`, `tick`, infrastructure access).
 *
 * ### Goals
 * - Replace manual `main.cpp` wiring with a declarative API.
 * - Hide kernel task construction — the user never sees ScheduledControlTask,
 *   ScheduledCommsTask, or BackgroundDiagnosticsTask directly.
 * - Populate `System<Cfg>` with the lock-free command queue, inter-core
 *   watchdog, and kernel tasks.
 * - Enforce type-safe task registration: `IScheduledTask` on cores via
 *   `addScheduledTask()`, `IBackgroundTask` via `addBackgroundTask()`.
 * - Zero heap allocation — all storage is statically sized from `Cfg`.
 *
 * ### Usage
 * @code
 *   // User-space application and safety monitors
 *   MyApp app;
 *   ArcDetectorMonitor arcMon(&arcDet);
 *   InterlockMonitor   interlockMon(&im);
 *   std::array<ISafetyMonitor*, 2> monitors = {&arcMon, &interlockMon};
 *
 *   // Build the kernel
 *   SystemBuilder<MyConfig> builder(&app, monitors.data(), monitors.size());
 *   builder.setStream(&stream);
 *   builder.setWatchdogKick(myWatchdogKickFn);
 *
 *   // Optionally add user tasks
 *   builder.core(0).addScheduledTask(&myCustomTask);
 *
 *   // Validate and finalize
 *   auto result = builder.build();
 *   if (!result) { return 1; }
 *
 *   // Runtime — driven through System static API
 *   System<MyConfig>::init(0);
 *   while (true) { System<MyConfig>::tick(0, getTimeMicros()); }
 * @endcode
 *
 * @tparam Cfg Configuration struct satisfying `ConfigValidator<Cfg>`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */

#include "sputteros/kernel/System.h"
#include "sputteros/kernel/interfaces/ISafetyMonitor.h"
#include "sputteros/kernel/interfaces/IUserApplication.h"
#include "sputteros/osal/tasks/IBackgroundTask.h"
#include "sputteros/osal/tasks/IScheduledTask.h"
#include "sputteros/osal/tasks/ITask.h"

#include <cassert>
#include <cstddef>

namespace SputterOS
{

// =========================================================================
// BuildResult
// =========================================================================

/**
 * @brief Outcome of `SystemBuilder::build()`.
 *
 * Contains a success flag and, on failure, a human-readable static error
 * string describing the first validation issue encountered.
 */
struct [[nodiscard]] BuildResult
{
    bool        ok;    /**< @brief true if the build passed all checks. */
    const char *error; /**< @brief Human-readable error, or nullptr on success. */

    /**
     * @brief Convenience bool conversion for `if (result) { ... }`.
     */
    explicit operator bool() const { return ok; }
};

// =========================================================================
// Forward declaration
// =========================================================================

template <typename Cfg> class SystemBuilder;

// =========================================================================
// CoreBuilder
// =========================================================================

/**
 * @brief Per-core topology declaration helper.
 *
 * Returned by `SystemBuilder::core(coreId)`. Users chain calls to
 * `addTask()` to declare which tasks run on this core. Returns a
 * reference to itself for fluent chaining, with `done()` returning
 * back to the parent `SystemBuilder`.
 *
 * Internally wraps a `System<Cfg>::CoreData` reference — task
 * registrations write directly into the `System` singleton's static
 * per-core storage.
 *
 * @note The kernel no longer tracks hardware ownership. Device management
 *       is a user-space concern — wrap your devices in `ISafetyMonitor`
 *       or manage them inside `IUserApplication`.
 *
 * @tparam Cfg Configuration struct.
 */
template <typename Cfg> class CoreBuilder
{
  public:
    static constexpr std::size_t kMaxTasks = kMaxTasksPerCore;

    // -----------------------------------------------------------------------
    // Fluent API
    // -----------------------------------------------------------------------

    /**
     * @brief Register a scheduled task to run on this core.
     * @param task Non-null `IScheduledTask` pointer whose lifetime must
     *        exceed the system runtime.
     * @return Reference to this builder for chaining.
     */
    CoreBuilder &addScheduledTask(IScheduledTask *task)
    {
        if (m_core && task)
        {
            m_core->addTask(task);
        }
        return *this;
    }

    /**
     * @brief Return to the parent `SystemBuilder` after configuring this core.
     * @return Reference to the parent builder for continued chaining.
     */
    SystemBuilder<Cfg> &done() { return *m_parent; }

    // -----------------------------------------------------------------------
    // Queries (used internally by SystemBuilder::build)
    // -----------------------------------------------------------------------

    /** @brief Number of tasks registered on this core. */
    std::size_t taskCount() const { return m_core ? m_core->taskCount : 0; }

    /** @brief Get task pointer by index. */
    ITask *task(std::size_t idx) const { return m_core ? m_core->task(idx) : nullptr; }

    /** @brief Whether this core has any tasks registered. */
    bool isActive() const { return m_core && m_core->isActive(); }

  private:
    friend class SystemBuilder<Cfg>;

    /**
     * @brief Insert a task at the front of the task list, shifting others right.
     *
     * Used internally by `SystemBuilder::build()` to ensure kernel tasks
     * run before user tasks in the tick order (safety evaluation first).
     */
    void prependTask(ITask *task)
    {
        if (m_core)
        {
            m_core->prependTask(task);
        }
    }

    /**
     * @brief Private constructor — only `SystemBuilder` creates `CoreBuilder` instances.
     */
    explicit CoreBuilder(SystemBuilder<Cfg> *parent = nullptr, typename System<Cfg>::CoreData *core = nullptr)
        : m_parent(parent), m_core(core)
    {
    }

    SystemBuilder<Cfg>             *m_parent;
    typename System<Cfg>::CoreData *m_core;
};

// =========================================================================
// SystemBuilder
// =========================================================================

/**
 * @brief Configuration and validation builder for SputterOS.
 *
 * Declares the system topology, validates core affinity, and populates
 * the `System<Cfg>` singleton with kernel tasks and monitored task
 * lists. After a successful `build()`, all runtime operations are
 * driven through `System<Cfg>` static member functions.
 *
 * The builder itself holds only user-provided kernel dependencies
 * (`IUserApplication`, `ISafetyMonitor`, stream, watchdog-kick).
 * All runtime infrastructure (command queue, watchdog, sync, error
 * logger, memory profiler, kernel tasks, per-core task lists) lives
 * in `System<Cfg>` as `inline static` data members.
 *
 * @tparam Cfg Configuration struct satisfying `ConfigValidator<Cfg>`.
 */
template <typename Cfg> class SystemBuilder
{
    // Validate the configuration at instantiation time.
    static_assert(ConfigValidator<Cfg>::value, "Invalid SputterOS configuration.");

    using S = System<Cfg>;

  public:
    static constexpr std::size_t kCoreCount     = Cfg::kCoreCount;
    static constexpr std::size_t kQueueCapacity = Cfg::kQueueCapacity;
    static constexpr bool        kMultiCore     = (kCoreCount >= 2);

    /**
     * @brief Conditional MultiCoreSync type.
     *
     * `MultiCoreSync<N>` requires N >= 2. For single-core systems,
     * `NoOpMultiCoreSync` is used instead.
     */
    using SyncType = typename S::SyncType;

    // -----------------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------------

    /**
     * @brief Construct a SystemBuilder with kernel dependencies.
     *
     * When `app` is non-null, `build()` will create the three kernel
     * tasks (ScheduledControlTask, ScheduledCommsTask,
     * BackgroundDiagnosticsTask) and register them on the correct cores.
     * When `app` is null, the builder operates in infrastructure-only
     * mode for build-time smoke tests.
     *
     * @param app: User application ticked by the kernel each cycle (nullable).
     * @param monitors: Array of safety monitors evaluated each tick (nullable if count is 0).
     * @param monitorCount: Number of elements in the monitors array.
     */
    SystemBuilder(IUserApplication<Cfg> *app = nullptr, ISafetyMonitor **monitors = nullptr,
                  std::size_t monitorCount = 0)
        : m_app(app), m_monitors(monitors), m_monitorCount(monitorCount), m_stream(nullptr), m_watchdogKick(nullptr),
          m_clockSource(nullptr)
    {
        for (std::size_t i = 0; i < kCoreCount; ++i)
        {
            m_cores[i] = CoreBuilder<Cfg>(this, &S::s_cores[i]);
        }
    }

    // Non-copyable, non-movable — singleton builder.
    SystemBuilder(const SystemBuilder &)            = delete;
    SystemBuilder &operator=(const SystemBuilder &) = delete;

    // -----------------------------------------------------------------------
    // Kernel dependency setters (fluent, pre-build)
    // -----------------------------------------------------------------------

    /**
     * @brief Set the byte stream for the CommsTask.
     * @param stream: Bidirectional byte stream (USB CDC, UART, etc.).
     * @return Reference to this builder for chaining.
     */
    SystemBuilder &setStream(IStream *stream)
    {
        m_stream = stream;
        return *this;
    }

    /**
     * @brief Set the platform-specific watchdog kick function.
     *
     * The watchdog kick function is the only user-injected diagnostics
     * dependency. `ErrorLogger` and `MemoryProfiler` are kernel-owned
     * and embedded in the `System` singleton.
     *
     * @param watchdogKick: Platform callback to kick the hardware watchdog (nullable).
     * @return Reference to this builder for chaining.
     */
    SystemBuilder &setWatchdogKick(typename Kernel::BackgroundDiagnosticsTask::WatchdogKickFn watchdogKick)
    {
        m_watchdogKick = watchdogKick;
        return *this;
    }

    /**
     * @brief Set the platform microsecond clock source.
     *
     * The clock source is propagated to `System<Cfg>::timer()` and to
     * every `TaskTimer` for per-task instrumentation. Must return a
     * monotonically increasing `uint64_t` count of microseconds.
     *
     * @param src: Platform clock function pointer (see `MicrosecondSource`).
     * @return Reference to this builder for chaining.
     */
    SystemBuilder &setClockSource(MicrosecondSource src)
    {
        m_clockSource = src;
        return *this;
    }

    // -----------------------------------------------------------------------
    // Topology declaration
    // -----------------------------------------------------------------------

    /**
     * @brief Access the `CoreBuilder` for a given core ID.
     *
     * Use this to register additional user scheduled tasks on specific
     * cores. Kernel tasks are auto-registered by `build()` and do not
     * need to be added manually.
     *
     * @param coreId Zero-based core identifier. Must be < kCoreCount.
     * @return Reference to the per-core builder for fluent chaining.
     */
    CoreBuilder<Cfg> &core(std::size_t coreId) { return m_cores[coreId < kCoreCount ? coreId : 0]; }

    /**
     * @brief Register a background task (no core affinity).
     *
     * Background tasks run in idle time with a budget cap. They are
     * stored in `System<Cfg>::s_backgroundTasks[]`.
     *
     * @param task Non-null `IBackgroundTask` pointer whose lifetime must
     *        exceed the system runtime.
     * @return Reference to this builder for chaining.
     */
    SystemBuilder &addBackgroundTask(IBackgroundTask *task)
    {
        if (task && S::s_backgroundTaskCount < CfgMaxBackgroundTasks<Cfg>::value)
        {
            S::s_backgroundTasks[S::s_backgroundTaskCount++] = task;
        }
        return *this;
    }

    // -----------------------------------------------------------------------
    // Build
    // -----------------------------------------------------------------------

    /**
     * @brief Validate the declared topology, create kernel tasks, and
     *        populate the `System<Cfg>` singleton for execution.
     *
     * When `app` is non-null, the following kernel tasks are created
     * and pinned automatically:
     * - `ScheduledControlTask` (IScheduledTask) → Core 0
     * - `ScheduledCommsTask` (IScheduledTask) → Core 1 (or Core 0 if single-core)
     * - `BackgroundDiagnosticsTask` (IBackgroundTask) → background ring
     *   (also temporarily on a core for Phase 1 dispatch compatibility)
     *
     * Performs validation:
     * 1. At least one core has tasks (or kernel tasks are being created).
     * 2. Scheduled task period ≥ `CfgMinSchedulePeriodUs<Cfg>`.
     * 3. No duplicate task pointer across cores.
     * 4. Task dependency validation.
     *
     * @return `BuildResult` with `ok == true` on success, or a descriptive
     *         error string on failure.
     */
    [[nodiscard]] BuildResult build()
    {
        if (S::s_built)
        {
            return {false, "SystemBuilder::build() already called"};
        }

        // --- Create kernel tasks if application is provided ---
        if (m_app)
        {
            S::s_controlTask.emplace(Kernel::KernelConstructTag{}, &S::s_commandQueue, m_app, m_monitors,
                                     m_monitorCount);
            S::s_commsTask.emplace(Kernel::KernelConstructTag{}, m_stream, &S::s_commandQueue);
            S::s_diagsTask.emplace(Kernel::KernelConstructTag{}, S::s_errorLogger, S::s_memProfiler, m_watchdogKick,
                                   CfgControlBudgetUs<Cfg>::value);

            // Auto-register kernel scheduled tasks on correct cores.
            // Prepend in reverse order so the final tick order is:
            //   [ScheduledControlTask, ScheduledCommsTask, BackgroundDiagnosticsTask, ...user tasks...]
            if constexpr (kMultiCore)
            {
                m_cores[0].prependTask(&*S::s_controlTask);
                // TODO: Phase 3 — remove DiagnosticsTask from core task list when SystemScheduler dispatches background
                // tasks
                m_cores[1].prependTask(&*S::s_diagsTask);
                m_cores[1].prependTask(&*S::s_commsTask);
            }
            else
            {
                // TODO: Phase 3 — remove DiagnosticsTask from core task list when SystemScheduler dispatches background
                // tasks
                m_cores[0].prependTask(&*S::s_diagsTask);
                m_cores[0].prependTask(&*S::s_commsTask);
                m_cores[0].prependTask(&*S::s_controlTask);
            }

            // Register BackgroundDiagnosticsTask in the background task ring.
            S::s_backgroundTasks[S::s_backgroundTaskCount++] = &*S::s_diagsTask;
        }

        // --- Check that at least one core has tasks ---
        bool anyActive = false;
        for (std::size_t c = 0; c < kCoreCount; ++c)
        {
            if (m_cores[c].isActive())
            {
                anyActive = true;
                break;
            }
        }
        if (!anyActive)
        {
            return {false, "No tasks registered on any core"};
        }

        // --- Scheduling constraint validation ---
        {
            BuildResult schedResult = validateSchedulingConstraints();
            if (!schedResult.ok)
            {
                return schedResult;
            }
        }

        // --- Task dependency validation ---
        {
            BuildResult depResult = validateTaskDependencies();
            if (!depResult.ok)
            {
                return depResult;
            }
        }

        // --- Build the flat monitored-task list for DiagnosticsTask ---
        if (m_app && S::s_diagsTask.has_value())
        {
            S::s_allTaskCount = 0;
            for (std::size_t c = 0; c < kCoreCount; ++c)
            {
                for (std::size_t t = 0; t < S::s_cores[c].taskCount; ++t)
                {
                    ITask *tsk = S::s_cores[c].tasks[t];
                    if (tsk && S::s_allTaskCount < S::kMaxTotalTasks)
                    {
                        S::s_allTasks[S::s_allTaskCount++] = tsk;
                    }
                }
            }
            S::s_diagsTask->setMonitoredTasks(S::s_allTasks, S::s_allTaskCount);
        }

        // --- Propagate clock source to SystemTimer and all TaskTimers ---
        if (m_clockSource)
        {
            S::s_timer.setClockSource(m_clockSource);
            for (std::size_t c = 0; c < kCoreCount; ++c)
            {
                for (std::size_t t = 0; t < S::s_cores[c].taskCount; ++t)
                {
                    ITask *tsk = S::s_cores[c].tasks[t];
                    if (tsk)
                    {
                        tsk->timer().setClockSource(m_clockSource);
                    }
                }
            }
        }

        S::s_built = true;
        S::transitionTo(Kernel::KernelState::CONFIGURED);
        return {true, nullptr};
    }

  private:
    // -----------------------------------------------------------------------
    // Core affinity validation
    // -----------------------------------------------------------------------

    /**
     * @brief Validate scheduling constraints for all registered tasks.
     *
     * Checks:
     * 1. Period floor: every `IScheduledTask` must have `periodUs() >=
     *    CfgMinSchedulePeriodUs<Cfg>`.
     * 2. No duplicate task registration (same pointer on multiple slots/cores).
     * 3. Utilization check: warn if per-core utilization exceeds 80%
     *    (logged to `ErrorLogger`, does not fail the build).
     *
     * @return BuildResult with ok=false if a hard constraint is violated.
     */
    BuildResult validateSchedulingConstraints() const
    {
        // --- Period floor check ---
        for (std::size_t c = 0; c < kCoreCount; ++c)
        {
            for (std::size_t t = 0; t < m_cores[c].taskCount(); ++t)
            {
                ITask *tsk = m_cores[c].task(t);
                if (tsk && tsk->isScheduled())
                {
                    auto *scheduled = static_cast<IScheduledTask *>(tsk);
                    if (scheduled->periodUs() > 0 && scheduled->periodUs() < CfgMinSchedulePeriodUs<Cfg>::value)
                    {
                        return {false, "Scheduled task period below minimum"};
                    }
                }
            }
        }

        // --- No duplicate task registration ---
        for (std::size_t c1 = 0; c1 < kCoreCount; ++c1)
        {
            for (std::size_t t1 = 0; t1 < m_cores[c1].taskCount(); ++t1)
            {
                ITask *tsk1 = m_cores[c1].task(t1);
                if (!tsk1)
                    continue;
                for (std::size_t c2 = c1; c2 < kCoreCount; ++c2)
                {
                    std::size_t startT = (c2 == c1) ? t1 + 1 : 0;
                    for (std::size_t t2 = startT; t2 < m_cores[c2].taskCount(); ++t2)
                    {
                        if (m_cores[c2].task(t2) == tsk1)
                        {
                            return {false, "Duplicate task registration detected"};
                        }
                    }
                }
            }
        }

        // --- Utilization warning (best-effort, Phase 1) ---
        for (std::size_t c = 0; c < kCoreCount; ++c)
        {
            uint64_t    numerator      = 0; // sum of (wcet * 1000 / period)
            std::size_t scheduledCount = 0;

            for (std::size_t t = 0; t < m_cores[c].taskCount(); ++t)
            {
                ITask *tsk = m_cores[c].task(t);
                if (tsk && tsk->isScheduled())
                {
                    auto         *scheduled = static_cast<IScheduledTask *>(tsk);
                    SputterMicros wcet      = scheduled->declaredWcetUs();
                    SputterMicros period    = scheduled->periodUs();
                    if (wcet > 0 && period > 0)
                    {
                        // Accumulate utilization as (wcet / period) scaled by 1000
                        numerator += (wcet * 1000) / period;
                        ++scheduledCount;
                    }
                }
            }
            // If utilization > 0.8 (i.e. numerator > 800), log a warning
            if (scheduledCount > 0 && numerator > 800)
            {
                S::s_errorLogger.log(ErrorLogger::ErrorCode::SENSOR_ERROR, SputterMicros(0),
                                     static_cast<float>(numerator) / 1000.0f);
            }
        }

        return {true, nullptr};
    }

    /**
     * @brief Call `validateDependencies()` on every registered task.
     *
     * Iterates all tasks on all cores and verifies that each task's
     * required device dependencies have been satisfied. User tasks
     * override `ITask::validateDependencies()` to declare their
     * requirements. Kernel tasks check their injected pointers.
     *
     * @return BuildResult with ok=false if any task reports unmet dependencies.
     */
    BuildResult validateTaskDependencies() const
    {
        for (std::size_t c = 0; c < kCoreCount; ++c)
        {
            for (std::size_t t = 0; t < m_cores[c].taskCount(); ++t)
            {
                ITask *tsk = m_cores[c].task(t);
                if (tsk && !tsk->validateDependencies())
                {
                    return {false, "Task dependency validation failed"};
                }
            }
        }
        return {true, nullptr};
    }

    // -----------------------------------------------------------------------
    // User-provided kernel dependencies (setup-only)
    // -----------------------------------------------------------------------

    IUserApplication<Cfg> *m_app;          /**< @brief User application (nullable). */
    ISafetyMonitor       **m_monitors;     /**< @brief Safety monitor array. */
    std::size_t            m_monitorCount; /**< @brief Number of safety monitors. */
    IStream               *m_stream;       /**< @brief Byte stream for CommsTask. */

    typename Kernel::BackgroundDiagnosticsTask::WatchdogKickFn m_watchdogKick; /**< @brief Watchdog kick. */
    MicrosecondSource                                          m_clockSource;  /**< @brief Platform \u00b5s clock. */

    // -----------------------------------------------------------------------
    // Per-core builders (write into System<Cfg>::s_cores)
    // -----------------------------------------------------------------------

    CoreBuilder<Cfg> m_cores[kCoreCount];
};

} // namespace SputterOS

#endif // SPUTTEROS_BUILDER_SYSTEM_BUILDER_H
