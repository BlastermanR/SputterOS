#ifndef SENSORPOLL_OSAL_IMPL_SENSORPOLLOSAL_H
#define SENSORPOLL_OSAL_IMPL_SENSORPOLLOSAL_H

/**
 * @file SensorPollOSAL.h
 * @brief OS-native platform utilities for the SensorPoll example.
 *
 * Re-exports the shared host time source under the SensorPoll namespace.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "common/osal/HostTimeMicros.h"

namespace SensorPoll
{
using ExamplesCommon::platformGetTimeMicros;
} // namespace SensorPoll

#endif // SENSORPOLL_OSAL_IMPL_SENSORPOLLOSAL_H
