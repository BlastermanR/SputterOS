#ifndef MULTIRATE_HAL_IMPL_MULTIRATEHAL_H
#define MULTIRATE_HAL_IMPL_MULTIRATEHAL_H

/**
 * @file MultirateHAL.h
 * @brief HAL implementations for the MultiRate example.
 *
 * Re-exports the shared stub classes under the Multirate namespace.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "common/hal/AlwaysSafeSafetyMonitor.h"
#include "common/hal/StdoutStreamReader.h"

namespace Multirate
{
using StdoutStreamReader      = ExamplesCommon::StdoutStreamReader;
using AlwaysSafeSafetyMonitor = ExamplesCommon::AlwaysSafeSafetyMonitor;
} // namespace Multirate

#endif // MULTIRATE_HAL_IMPL_MULTIRATEHAL_H
