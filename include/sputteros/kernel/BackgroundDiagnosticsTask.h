#ifndef SPUTTEROS_KERNEL_BACKGROUNDDIAGNOSTICSTASK_H
#define SPUTTEROS_KERNEL_BACKGROUNDDIAGNOSTICSTASK_H

#include "sputteros/kernel/KernelConstructTag.h"
#include "sputteros/kernel/SchedulerHealthMetrics.h"
#include "sputteros/kernel/TaskTimer.h"
#include "sputteros/osal/tasks/IBackgroundTask.h"
#include "sputteros/utils/MemoryProfiler.h"
#include "sputteros/utils/QueueDepthMonitor.h"
#include "sputteros/utils/logging/ErrorLogger.h"
#include <cstddef>
#include <cstdint>

/**
 * @file BackgroundDiagnosticsTask.h
 * @brief Kernel health monitor, watchdog, per-task timing, and memory profiling.
 *
 * Monitors per-task execution times via `TaskTimer` instances embedded in
 * each `ITask`, maintains the kernel-owned `ErrorLogger` ring buffer,
 * tracks heap and stack high-water marks via the kernel-owned
 * `MemoryProfiler`, and kicks the hardware watchdog timer to prevent a
 * system reset.
 *
 * `BackgroundDiagnosticsTask` aggregates timing data from all registered tasks:
 * each `ITask` has a `TaskTimer` that `System::tick()` instruments
 * automatically. This task reads those timers every cycle, logging any
 * budget violations to `ErrorLogger`.
 *
 * @note This is a kernel task. Its constructor requires a `KernelConstructTag`,
 *       only `SystemBuilder` and `KernelTestAccess` may instantiate it.
 * @note `ErrorLogger` and `MemoryProfiler` are kernel-owned members of
 *       `System`. Users access them via `System<Cfg>::errorLogger()` and
 *       `System<Cfg>::memProfiler()`.
 * @note Inherits `IBackgroundTask` — pinned to Core 1 in multi-core configs.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */
namespace SputterOS
{

class ITask;
template <typename Cfg> class SystemBuilder;

namespace Kernel
{

struct KernelTestAccess;

class BackgroundDiagnosticsTask : public IBackgroundTask
{
  public:
    /**
     * @brief Watchdog kick callback type.
     */
    using WatchdogKickFn = void (*)();

    /**
     * @brief Maximum execution budget per dispatch in microseconds.
     * @return Budget from the control cycle budget parameter.
     */
    SputterMicros maxBudgetUs() const override { return m_controlBudget; }

    /**
     * @brief Initialize diagnostic subsystems and record baseline memory usage.
     */
    void init() override;

    /**
     * @brief Execute one diagnostics cycle.
     *
     * Sequence per tick:
     * 1. Kick the hardware watchdog.
     * 2. Scan all monitored task timers for budget violations; log overruns.
     * 3. Update memory profiler heap/stack high-water marks.
     * 4. Every `kMemCheckInterval` ticks, log a memory health snapshot.
     *
     * @param systemTime: Monotonic system time forwarded from the scheduler.
     */
    void tick(SputterMicros systemTimeMicros) override;

    /**
     * @brief Register the array of tasks whose timers should be monitored.
     *
     * Called by `SystemBuilder` after `build()` finalizes the task list.
     * Must be called before the first `tick()`.
     *
     * @param tasks: Array of `ITask` pointers to monitor.
     * @param count: Number of elements in the array.
     */
    void setMonitoredTasks(ITask *const *tasks, std::size_t count);

    /**
     * @brief Set the queue depth monitor to sample each tick.
     * @param monitor: Pointer to the kernel-owned QueueDepthMonitor.
     */
    void setQueueMonitor(QueueDepthMonitor *monitor) { m_queueMonitor = monitor; }

    /**
     * @brief Set the scheduler health metrics to update each tick.
     * @param metrics: Pointer to the kernel-owned SchedulerHealthMetrics.
     */
    void setSchedulerHealth(SchedulerHealthMetrics *metrics) { m_schedulerHealth = metrics; }

    /**
     * @brief PassKey constructor — only SystemBuilder and KernelTestAccess may instantiate.
     *
     * @param tag: Opaque access token (see KernelConstructTag.h).
     * @param logger: Kernel-owned error logger.
     * @param memProfiler: Kernel-owned memory profiler.
     * @param watchdogKick: Platform-specific watchdog kick function pointer.
     * @param controlBudgetUs: Target control cycle budget in microseconds (default 10000 = 10 ms).
     */
    BackgroundDiagnosticsTask(KernelConstructTag tag, ErrorLogger &logger, MemoryProfiler &memProfiler,
                              WatchdogKickFn watchdogKick, uint32_t controlBudgetUs = 10000);

  private:
    template <typename> friend class SystemBuilder;
    friend struct KernelTestAccess;

    /**
     * --------------------
     * Kernel-Owned Dependencies (references)
     * --------------------
     */
    ErrorLogger    &m_logger;       /**< @brief Kernel-owned diagnostic log. */
    MemoryProfiler &m_memProfiler;  /**< @brief Kernel-owned heap/stack tracker. */
    WatchdogKickFn  m_watchdogKick; /**< @brief Platform WDT kick routine. */

    /**
     * --------------------
     * Monitored Task List
     * --------------------
     */
    static constexpr std::size_t kMaxMonitoredTasks = 16;
    ITask                       *m_monitoredTasks[kMaxMonitoredTasks]; /**< @brief Tasks to read timers from. */
    std::size_t                  m_monitoredCount;                     /**< @brief Number of monitored tasks. */

    /**
     * --------------------
     * Budget Enforcement
     * --------------------
     */
    SputterMicros m_controlBudget;

    /**
     * --------------------
     * Tick Counters
     * --------------------
     */
    static constexpr uint32_t kMemCheckInterval = 100;
    uint32_t                  m_tickCount; /**< @brief Incremented each tick for sub-rate scheduling. */
    QueueDepthMonitor        *m_queueMonitor{nullptr}; /**< @brief Optional queue depth sampler. */
    SchedulerHealthMetrics    *m_schedulerHealth{nullptr}; /**< @brief Optional scheduler health aggregator. */
};

} // namespace Kernel
} // namespace SputterOS

#endif // SPUTTEROS_KERNEL_BACKGROUNDDIAGNOSTICSTASK_H
