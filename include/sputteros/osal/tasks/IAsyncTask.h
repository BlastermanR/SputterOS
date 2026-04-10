#ifndef SPUTTEROS_OSAL_IASYNCTASK_H
#define SPUTTEROS_OSAL_IASYNCTASK_H

#include "sputteros/osal/tasks/ITask.h"

/**
 * @file IAsyncTask.h
 * @brief Interface for best-effort, Core 1 pinned kernel tasks.
 *
 * Tasks that inherit `IAsyncTask` are scheduled on Core 1 in a multi-core
 * configuration. They do not have strict deterministic timing requirements
 * and may run on a best-effort basis. `SystemBuilder` enforces the core
 * pinning constraint at build-time.
 *
 * `CommsTask` and `DiagnosticsTask` are the canonical `IAsyncTask`
 * implementations — serial I/O and health monitoring do not require the
 * hard real-time guarantees of the control loop.
 *
 * @note In single-core configurations all tasks run on Core 0 regardless
 *       of their affinity marker.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */
namespace SputterOS
{

class IAsyncTask : public ITask
{
  public:
    /**
     * @brief Core affinity marker — always returns true for async tasks.
     */
    bool isAsync() const final { return true; }
};

} // namespace SputterOS

#endif // SPUTTEROS_OSAL_IASYNCTASK_H
