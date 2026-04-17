#ifndef PINGPONG_OSAL_IMPL_TIMEDMUTEXADAPTER_H
#define PINGPONG_OSAL_IMPL_TIMEDMUTEXADAPTER_H

/**
 * @file TimedMutexAdapter.h
 * @brief Re-exports the shared TimedMutexAdapter for the PingPong example.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/12/2026
 */

#include "common/osal/TimedMutexAdapter.h"

namespace PingPong
{
using TimedMutexAdapter = ExamplesCommon::TimedMutexAdapter;
} // namespace PingPong

#endif // PINGPONG_OSAL_IMPL_TIMEDMUTEXADAPTER_H
