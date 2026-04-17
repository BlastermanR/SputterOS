#ifndef LIFECYCLE_OSAL_IMPL_LIFECYCLEOSAL_H
#define LIFECYCLE_OSAL_IMPL_LIFECYCLEOSAL_H

/**
 * @file LifecycleOSAL.h
 * @brief OS-native platform utilities for the Lifecycle example.
 *
 * Re-exports the shared host time source under the Lifecycle namespace.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "common/osal/HostTimeMicros.h"

namespace Lifecycle
{
using ExamplesCommon::platformGetTimeMicros;
} // namespace Lifecycle

#endif // LIFECYCLE_OSAL_IMPL_LIFECYCLEOSAL_H
