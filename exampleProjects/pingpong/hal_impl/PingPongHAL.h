#ifndef PINGPONG_HAL_IMPL_PINGPONGHAL_H
#define PINGPONG_HAL_IMPL_PINGPONGHAL_H

/**
 * @file PingPongHAL.h
 * @brief HAL implementations for the PingPong dual-core example.
 *
 * Re-exports the shared stub classes under the PingPong namespace.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "common/hal/AlwaysSafeSafetyMonitor.h"
#include "common/hal/StdoutStreamReader.h"

namespace PingPong
{
using StdoutStreamReader      = ExamplesCommon::StdoutStreamReader;
using AlwaysSafeSafetyMonitor = ExamplesCommon::AlwaysSafeSafetyMonitor;
} // namespace PingPong

#endif // PINGPONG_HAL_IMPL_PINGPONGHAL_H
