#ifndef SPUTTEROS_OSAL_ICRITICALTASK_H
#define SPUTTEROS_OSAL_ICRITICALTASK_H

/**
 * @file ICriticalTask.h
 * @brief TEMPORARY compatibility shim — will be removed after Plan E.
 *
 * Bridges the old `ICriticalTask` API to the new `IScheduledTask`
 * hierarchy so that downstream code (`ControlTask`, tests) continues
 * to compile while the task hierarchy refactor is in progress.
 *
 * Provides a default `periodUs()` of 0 so concrete subclasses that
 * have not yet declared their period can still be instantiated.
 *
 * @warning Do NOT add new code that depends on `ICriticalTask`. Use
 *          `IScheduledTask` directly in all new code.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "sputteros/osal/tasks/IScheduledTask.h"

namespace SputterOS
{

class ICriticalTask : public IScheduledTask
{
  public:
    /** @brief Virtual destructor. */
    virtual ~ICriticalTask() = default;

    /**
     * @brief Default period — temporary stub until Plan E assigns real periods.
     * @return 0 (unconfigured).
     */
    SputterMicros periodUs() const override { return 0; }
};

} // namespace SputterOS

#endif // SPUTTEROS_OSAL_ICRITICALTASK_H
