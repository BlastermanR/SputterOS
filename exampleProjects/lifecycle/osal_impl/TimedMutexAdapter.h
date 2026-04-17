#ifndef LIFECYCLE_OSAL_IMPL_TIMEDMUTEXADAPTER_H
#define LIFECYCLE_OSAL_IMPL_TIMEDMUTEXADAPTER_H

/**
 * @file TimedMutexAdapter.h
 * @brief Re-exports the shared TimedMutexAdapter for the Lifecycle example.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/12/2026
 */

#include "common/osal/TimedMutexAdapter.h"

namespace Lifecycle
{
using TimedMutexAdapter = ExamplesCommon::TimedMutexAdapter;
} // namespace Lifecycle

#endif // LIFECYCLE_OSAL_IMPL_TIMEDMUTEXADAPTER_H
