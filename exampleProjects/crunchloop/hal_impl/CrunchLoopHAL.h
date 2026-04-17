#ifndef CRUNCHLOOP_HAL_IMPL_CRUNCHLOOPHAL_H
#define CRUNCHLOOP_HAL_IMPL_CRUNCHLOOPHAL_H

/**
 * @file CrunchLoopHAL.h
 * @brief HAL implementations for the CrunchLoop example.
 *
 * Re-exports the shared stub classes under the CrunchLoop namespace.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/14/2026
 */

#include "common/hal/AlwaysSafeSafetyMonitor.h"
#include "common/hal/StdoutStreamReader.h"

namespace CrunchLoop
{
using StdoutStreamReader      = ExamplesCommon::StdoutStreamReader;
using AlwaysSafeSafetyMonitor = ExamplesCommon::AlwaysSafeSafetyMonitor;
} // namespace CrunchLoop

#endif // CRUNCHLOOP_HAL_IMPL_CRUNCHLOOPHAL_H
