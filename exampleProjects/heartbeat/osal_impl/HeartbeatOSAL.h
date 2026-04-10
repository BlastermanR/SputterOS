#ifndef HEARTBEAT_OSAL_IMPL_HEARTBEATOSAL_H
#define HEARTBEAT_OSAL_IMPL_HEARTBEATOSAL_H

/**
 * @file HeartbeatOSAL.h
 * @brief OS-native platform utilities for the HeartBeat system test.
 *
 * Provides the `platformGetTimeMicros()` time-source function used by
 * `main_osNative.cpp`.  On the OS-native platform, time is read from
 * `std::chrono::steady_clock` and converted to microseconds.
 *
 * For a Pico variant, replace this file with one that wraps
 * `to_us_since_boot(get_absolute_time())` from the Pico SDK.
 * No other file changes are needed.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */

#include "sputteros/osal/SputterTime.h"
#include <chrono>
#include <cstdint>

namespace Heartbeat
{

/**
 * @brief Return the current monotonic time as a `SputterMicros` value.
 *
 * Reads `std::chrono::steady_clock`, duration-casts to microseconds.
 * 64-bit µs wraps at ~584,942 years — effectively never.
 *
 * @return Current time in microseconds.
 */
inline SputterOS::SputterMicros platformGetTimeMicros()
{
    using namespace std::chrono;
    return static_cast<SputterOS::SputterMicros>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

} // namespace Heartbeat

#endif // HEARTBEAT_OSAL_IMPL_HEARTBEATOSAL_H
