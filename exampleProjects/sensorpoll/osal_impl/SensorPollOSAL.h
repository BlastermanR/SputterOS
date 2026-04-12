#ifndef SENSORPOLL_OSAL_IMPL_SENSORPOLLOSAL_H
#define SENSORPOLL_OSAL_IMPL_SENSORPOLLOSAL_H

/**
 * @file SensorPollOSAL.h
 * @brief OS-native platform utilities for the SensorPoll example.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "sputteros/osal/SputterTime.h"
#include <chrono>
#include <cstdint>

namespace SensorPoll
{

/**
 * @brief Return the current monotonic time as a `SputterMicros` value.
 * @return Current time in microseconds.
 */
inline SputterOS::SputterMicros platformGetTimeMicros()
{
    using namespace std::chrono;
    return static_cast<SputterOS::SputterMicros>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

} // namespace SensorPoll

#endif // SENSORPOLL_OSAL_IMPL_SENSORPOLLOSAL_H
