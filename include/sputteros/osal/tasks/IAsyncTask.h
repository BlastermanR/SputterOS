#ifndef SPUTTEROS_OSAL_IASYNCTASK_H
#define SPUTTEROS_OSAL_IASYNCTASK_H

/**
 * @file IAsyncTask.h
 * @brief TEMPORARY compatibility shim — will be removed after Plan E.
 *
 * Bridges the old `IAsyncTask` API to the new `IBackgroundTask`
 * hierarchy so that downstream code (`CommsTask`, `DiagnosticsTask`,
 * tests) continues to compile while the task hierarchy refactor is
 * in progress.
 *
 * @warning Do NOT add new code that depends on `IAsyncTask`. Use
 *          `IBackgroundTask` directly in all new code.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "sputteros/osal/tasks/IBackgroundTask.h"

namespace SputterOS
{

class IAsyncTask : public IBackgroundTask
{
  public:
    /** @brief Virtual destructor. */
    virtual ~IAsyncTask() = default;
};

} // namespace SputterOS

#endif // SPUTTEROS_OSAL_IASYNCTASK_H
