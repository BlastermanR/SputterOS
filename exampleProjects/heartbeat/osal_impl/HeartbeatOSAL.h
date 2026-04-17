#ifndef HEARTBEAT_OSAL_IMPL_HEARTBEATOSAL_H
#define HEARTBEAT_OSAL_IMPL_HEARTBEATOSAL_H

/**
 * @file HeartbeatOSAL.h
 * @brief OS-native platform utilities for the HeartBeat example.
 *
 * Re-exports the shared host time source under the Heartbeat namespace.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */

#include "common/osal/HostTimeMicros.h"

namespace Heartbeat
{
using ExamplesCommon::platformGetTimeMicros;
} // namespace Heartbeat

#endif // HEARTBEAT_OSAL_IMPL_HEARTBEATOSAL_H
