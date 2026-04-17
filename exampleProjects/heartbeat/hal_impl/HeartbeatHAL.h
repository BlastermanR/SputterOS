#ifndef HEARTBEAT_HAL_IMPL_HEARTBEATHAL_H
#define HEARTBEAT_HAL_IMPL_HEARTBEATHAL_H

/**
 * @file HeartbeatHAL.h
 * @brief HAL implementations for the HeartBeat example.
 *
 * Re-exports the shared stub classes under the Heartbeat namespace.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */

#include "common/hal/AlwaysSafeSafetyMonitor.h"
#include "common/hal/StdoutStreamReader.h"

namespace Heartbeat
{
using StdoutStreamReader      = ExamplesCommon::StdoutStreamReader;
using AlwaysSafeSafetyMonitor = ExamplesCommon::AlwaysSafeSafetyMonitor;
} // namespace Heartbeat

#endif // HEARTBEAT_HAL_IMPL_HEARTBEATHAL_H
