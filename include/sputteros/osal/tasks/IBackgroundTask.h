#ifndef SPUTTEROS_OSAL_IBACKGROUNDTASK_H
#define SPUTTEROS_OSAL_IBACKGROUNDTASK_H

/**
 * @file IBackgroundTask.h
 * @brief Interface for best-effort background tasks.
 *
 * Tasks that inherit `IBackgroundTask` run only when no scheduled task
 * is ready. The Cruncher dispatches background tasks in idle time and
 * enforces a per-tick execution budget via `maxBudgetUs()` to avoid
 * starving scheduled work.
 *
 * Background tasks are suitable for serial I/O, telemetry, logging,
 * and other non-time-critical operations.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "sputteros/osal/SputterTime.h"
#include "sputteros/osal/tasks/ITask.h"

namespace SputterOS
{

/**
 * @brief Base class for best-effort, budget-capped background tasks.
 *
 * Non-copyable. Subclasses implement the `ITask` interface (`init()`,
 * `tick()`).
 */
class IBackgroundTask : public ITask
{
  public:
    /** @brief Virtual destructor. */
    virtual ~IBackgroundTask() = default;

    IBackgroundTask(const IBackgroundTask &)            = delete;
    IBackgroundTask &operator=(const IBackgroundTask &) = delete;

    /**
     * @brief Maximum execution budget per dispatch in microseconds.
     *
     * The Cruncher will stop the task's `tick()` accounting at this
     * limit. Override to increase or decrease the budget.
     *
     * @return Budget cap in microseconds (default: 1000 µs / 1 ms).
     */
    virtual SputterMicros maxBudgetUs() const { return 1000; }

    /**
     * @brief Scheduling type marker — always true for background tasks.
     */
    bool isBackground() const final { return true; }

    /**
     * @brief Scheduling type marker — always false for background tasks.
     */
    bool isScheduled() const final { return false; }

  protected:
    /** @brief Default constructor (protected — instantiate subclasses only). */
    IBackgroundTask() = default;
};

} // namespace SputterOS

#endif // SPUTTEROS_OSAL_IBACKGROUNDTASK_H
