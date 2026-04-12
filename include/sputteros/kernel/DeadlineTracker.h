#ifndef SPUTTEROS_KERNEL_DEADLINETRACKER_H
#define SPUTTEROS_KERNEL_DEADLINETRACKER_H

/**
 * @file DeadlineTracker.h
 * @brief Lightweight absolute-deadline tracker for periodic task scheduling.
 *
 * Each schedule slot owns a DeadlineTracker that maintains the next
 * activation timestamp, period, and phase offset. The Cruncher queries
 * `isDue()` every dispatch cycle and calls `advance()` after execution
 * to compute the next deadline. Missed periods are skipped to prevent
 * cascade catch-up storms.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "sputteros/osal/SputterTime.h"

namespace SputterOS
{
namespace Kernel
{

/**
 * @brief Absolute-deadline tracker for a single periodic task.
 *
 * POD-like struct with inline methods. All storage is member-local —
 * no heap allocation.
 */
struct DeadlineTracker
{
    SputterMicros nextActivation{0}; /**< @brief Absolute time of next run (µs). */
    SputterMicros periodUs{0};       /**< @brief Repetition period (µs). */
    SputterMicros phaseOffsetUs{0};  /**< @brief Initial phase offset from epoch (µs). */

    /**
     * @brief Initialize the tracker from a given start time.
     *
     * Sets `nextActivation` to `startTime + phaseOffsetUs`.
     *
     * @param startTime Epoch timestamp for the schedule (µs).
     */
    void init(SputterMicros startTime) { nextActivation = startTime + phaseOffsetUs; }

    /**
     * @brief Check whether the task is due for execution.
     * @param now Current monotonic timestamp (µs).
     * @return true if `now >= nextActivation`.
     */
    bool isDue(SputterMicros now) const { return now >= nextActivation; }

    /**
     * @brief Advance to the next deadline after execution.
     *
     * Adds one period and then skips any additional missed periods so
     * the next activation is always in the future relative to `now`.
     *
     * @param now Current monotonic timestamp (µs).
     */
    void advance(SputterMicros now)
    {
        nextActivation += periodUs;
        while (nextActivation <= now)
        {
            nextActivation += periodUs;
        }
    }

    /**
     * @brief Compute remaining time until the next activation.
     * @param now Current monotonic timestamp (µs).
     * @return Microseconds until next activation, or 0 if already overdue.
     */
    SputterMicros timeUntilNext(SputterMicros now) const { return (nextActivation > now) ? (nextActivation - now) : 0; }
};

} // namespace Kernel
} // namespace SputterOS

#endif // SPUTTEROS_KERNEL_DEADLINETRACKER_H
