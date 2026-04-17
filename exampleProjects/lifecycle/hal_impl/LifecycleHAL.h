#ifndef LIFECYCLE_HAL_IMPL_LIFECYCLEHAL_H
#define LIFECYCLE_HAL_IMPL_LIFECYCLEHAL_H

/**
 * @file LifecycleHAL.h
 * @brief HAL implementations for the Lifecycle example.
 *
 * Re-exports the shared stub classes under the Lifecycle namespace.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "common/hal/AlwaysSafeSafetyMonitor.h"
#include "common/hal/StdoutStreamReader.h"

namespace Lifecycle
{
using StdoutStreamReader      = ExamplesCommon::StdoutStreamReader;
using AlwaysSafeSafetyMonitor = ExamplesCommon::AlwaysSafeSafetyMonitor;
} // namespace Lifecycle

#endif // LIFECYCLE_HAL_IMPL_LIFECYCLEHAL_H
