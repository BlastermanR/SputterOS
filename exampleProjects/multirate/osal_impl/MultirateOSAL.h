#ifndef MULTIRATE_OSAL_IMPL_MULTIRATEOSAL_H
#define MULTIRATE_OSAL_IMPL_MULTIRATEOSAL_H

/**
 * @file MultirateOSAL.h
 * @brief OS-native platform utilities for the MultiRate example.
 *
 * Re-exports the shared host time source under the Multirate namespace.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "common/osal/HostTimeMicros.h"

namespace Multirate
{
using ExamplesCommon::platformGetTimeMicros;
} // namespace Multirate

#endif // MULTIRATE_OSAL_IMPL_MULTIRATEOSAL_H
