#ifndef CRUNCHLOOP_OSAL_IMPL_TIMEDMUTEXADAPTER_H
#define CRUNCHLOOP_OSAL_IMPL_TIMEDMUTEXADAPTER_H

/**
 * @file TimedMutexAdapter.h
 * @brief Re-exports the shared TimedMutexAdapter for the CrunchLoop example.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/14/2026
 */

#include "common/osal/TimedMutexAdapter.h"

namespace CrunchLoop
{
using TimedMutexAdapter = ExamplesCommon::TimedMutexAdapter;
} // namespace CrunchLoop

#endif // CRUNCHLOOP_OSAL_IMPL_TIMEDMUTEXADAPTER_H
