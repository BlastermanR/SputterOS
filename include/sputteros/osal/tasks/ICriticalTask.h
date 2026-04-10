#ifndef SPUTTEROS_OSAL_ICRITICALTASK_H
#define SPUTTEROS_OSAL_ICRITICALTASK_H

#include "sputteros/osal/tasks/ITask.h"

/**
 * @file ICriticalTask.h
 * @brief Interface for deterministic, Core 0 pinned kernel tasks.
 *
 * Tasks that inherit `ICriticalTask` are guaranteed to be scheduled on
 * Core 0 in a multi-core configuration. `SystemBuilder` enforces this
 * constraint at build-time and the type trait `IsCriticalTask<T>` enables
 * compile-time `static_assert` checks.
 *
 * `ControlTask` is the canonical `ICriticalTask` — it runs the safety
 * evaluation and user-application loop at a fixed, deterministic rate.
 *
 * @note In single-core configurations all tasks run on Core 0 regardless
 *       of their affinity marker.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */
namespace SputterOS
{

class ICriticalTask : public ITask
{
  public:
    /**
     * @brief Core affinity marker — always returns true for critical tasks.
     */
    bool isCritical() const final { return true; }
};

} // namespace SputterOS

#endif // SPUTTEROS_OSAL_ICRITICALTASK_H
