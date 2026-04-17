#ifndef CRUNCHLOOP_OSAL_IMPL_CRUNCHLOOPOSAL_H
#define CRUNCHLOOP_OSAL_IMPL_CRUNCHLOOPOSAL_H

/**
 * @file CrunchLoopOSAL.h
 * @brief OS-native platform utilities for the CrunchLoop example.
 *
 * Re-exports the shared host time source under the CrunchLoop namespace.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/14/2026
 */

#include "common/osal/HostTimeMicros.h"

namespace CrunchLoop
{
using ExamplesCommon::platformGetTimeMicros;
} // namespace CrunchLoop

#endif // CRUNCHLOOP_OSAL_IMPL_CRUNCHLOOPOSAL_H
