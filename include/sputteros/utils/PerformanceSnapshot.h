#ifndef SPUTTEROS_UTILS_PERFORMANCESNAPSHOT_H
#define SPUTTEROS_UTILS_PERFORMANCESNAPSHOT_H

/**
 * @file PerformanceSnapshot.h
 * @brief Aggregate performance data structure for system-wide metrics.
 *
 * `PerformanceSnapshot` is a plain value type that captures a
 * point-in-time snapshot of all performance metrics: per-task timing
 * with histogram distribution, per-core utilization, command queue
 * depth, memory usage, and scheduler health.
 *
 * Populated by `System<Cfg>::snapshot()` and consumed by
 * `PerformanceFormatter` for machine-parseable output.
 *
 * @note Zero-heap. All arrays are fixed-size. The struct is a value
 *       copy — safe to pass across threads after capture.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "sputteros/kernel/metrics/TaskTimer.h"
#include "sputteros/osal/SputterTime.h"
#include <cstddef>
#include <cstdint>

namespace SputterOS
{

/**
 * @brief Per-task timing snapshot.
 */
struct TaskSnapshot
{
    std::size_t   taskIndex;  /**< @brief Task index in the core task list. */
    std::size_t   coreId;     /**< @brief Core this task runs on. */
    SputterMicros lastUs;     /**< @brief Most recent tick duration (µs). */
    SputterMicros minUs;      /**< @brief Minimum tick duration since reset (µs). */
    SputterMicros maxUs;      /**< @brief Maximum tick duration since reset (µs). */
    float         avgUs;      /**< @brief Average tick duration (µs). */
    uint32_t      samples;    /**< @brief Number of timing samples. */
    uint32_t      overruns;   /**< @brief Budget overrun count. */
    uint32_t      misses;     /**< @brief Deadline miss count. */
    uint32_t      histogram[Kernel::TaskTimer::kHistogramBuckets]; /**< @brief Duration distribution. */
};

/** @brief Maximum tasks tracked in a single snapshot. */
static constexpr std::size_t kMaxSnapshotTasks = 16;

/** @brief Maximum cores tracked in a single snapshot. */
static constexpr std::size_t kMaxSnapshotCores = 4;

/**
 * @brief Complete system performance snapshot.
 *
 * All fields are value-copied at capture time. Safe to read from
 * any context after `System<Cfg>::snapshot()` returns.
 */
struct PerformanceSnapshot
{
    SputterMicros timestamp; /**< @brief Capture time (µs). */

    // -- Core utilization --
    std::size_t coreCount;                         /**< @brief Number of active cores. */
    float       coreUtilization[kMaxSnapshotCores]; /**< @brief Per-core utilization [0.0, 1.0]. */

    // -- Command queue --
    std::size_t queueDepth;    /**< @brief Current queue depth at capture. */
    std::size_t queueMaxDepth; /**< @brief Peak queue depth since reset. */
    float       queueAvgDepth; /**< @brief Average queue depth since reset. */

    // -- Memory --
    std::size_t peakHeapUsed;   /**< @brief Peak heap consumption (bytes). */
    std::size_t freeHeap;       /**< @brief Current free heap (bytes, platform-specific). */
    std::size_t stackHighWater; /**< @brief Stack high-water mark (bytes, platform-specific). */

    // -- Scheduler health --
    SputterMicros totalGapUs;          /**< @brief Cumulative gap time (µs). */
    SputterMicros maxGapUs;            /**< @brief Peak single-tick gap (µs). */
    float         avgGapUs;            /**< @brief Average gap per tick (µs). */
    uint32_t      schedulerTickCount;  /**< @brief Total ticks tracked. */
    uint32_t      totalOverruns;       /**< @brief Aggregate overruns across all tasks. */
    uint32_t      totalDeadlineMisses; /**< @brief Aggregate deadline misses across all tasks. */

    // -- Per-task details --
    std::size_t  taskCount;                       /**< @brief Number of task entries populated. */
    TaskSnapshot tasks[kMaxSnapshotTasks];         /**< @brief Per-task timing snapshots. */
};

} // namespace SputterOS

#endif // SPUTTEROS_UTILS_PERFORMANCESNAPSHOT_H
