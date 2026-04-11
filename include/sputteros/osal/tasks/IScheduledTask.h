#ifndef SPUTTEROS_OSAL_ISCHEDULEDTASK_H
#define SPUTTEROS_OSAL_ISCHEDULEDTASK_H

/**
 * @file IScheduledTask.h
 * @brief Interface for deadline-scheduled periodic tasks.
 *
 * Tasks that inherit `IScheduledTask` declare a fixed period and are
 * dispatched by the Cruncher using deadline-driven scheduling. Each
 * subclass must implement `periodUs()` to declare its activation
 * interval. Optional overrides allow declaring a worst-case execution
 * time and a static priority (defaults to RMS auto-assignment).
 *
 * The `isIoPending()` hook lets a task signal an early yield when it
 * is waiting for an asynchronous I/O operation. The scheduler will
 * transition the task to `TaskState::IO_PENDING` and revisit it on
 * the next dispatch cycle.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "sputteros/osal/SputterTime.h"
#include "sputteros/osal/tasks/ITask.h"
#include <cstdint>

namespace SputterOS
{

/**
 * @brief Base class for periodic, deadline-scheduled tasks.
 *
 * Non-copyable. Subclasses must implement `periodUs()` and the
 * `ITask` interface (`init()`, `tick()`).
 */
class IScheduledTask : public ITask
{
  public:
    /** @brief Virtual destructor. */
    virtual ~IScheduledTask() = default;

    IScheduledTask(const IScheduledTask &)            = delete;
    IScheduledTask &operator=(const IScheduledTask &) = delete;

    /**
     * @brief The task's activation period in microseconds.
     *
     * Must return a non-zero value. The Cruncher uses this to compute
     * absolute deadlines via `DeadlineTracker`.
     *
     * @return Period in microseconds.
     */
    virtual SputterMicros periodUs() const = 0;

    /**
     * @brief Declared worst-case execution time in microseconds.
     *
     * Used by the schedulability analyser during `SystemBuilder::build()`
     * to verify that the schedule is feasible. A return value of 0
     * means the runtime will auto-profile the WCET from observed
     * `TaskTimer` data.
     *
     * @return WCET in microseconds, or 0 for auto-profiling.
     */
    virtual SputterMicros declaredWcetUs() const { return 0; }

    /**
     * @brief Static priority for the scheduler.
     *
     * Lower numeric values indicate higher priority. The default value
     * of 0xFF signals the builder to auto-assign priority using
     * Rate-Monotonic Scheduling (RMS) — shorter period → higher priority.
     *
     * @note Override only when RMS ordering is insufficient (e.g. for
     *       safety-critical tasks that must preempt longer-period tasks).
     *
     * @return Priority level (0 = highest, 0xFF = auto-assign RMS).
     */
    virtual uint8_t schedulePriority() const { return 0xFF; }

    /**
     * @brief Signal that this task is yielding early for I/O.
     *
     * When this returns true the scheduler transitions the task to
     * `TaskState::IO_PENDING` and does not count the remaining budget
     * as wasted. The task will be re-dispatched when its next deadline
     * arrives.
     *
     * @return true if the task is waiting for asynchronous I/O.
     */
    virtual bool isIoPending() const { return false; }

    /**
     * @brief Scheduling type marker — always true for scheduled tasks.
     */
    bool isScheduled() const final { return true; }

    /**
     * @brief Scheduling type marker — always false for scheduled tasks.
     */
    bool isBackground() const final { return false; }

  protected:
    /** @brief Default constructor (protected — instantiate subclasses only). */
    IScheduledTask() = default;
};

} // namespace SputterOS

#endif // SPUTTEROS_OSAL_ISCHEDULEDTASK_H
