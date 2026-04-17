#ifndef SPUTTEROS_EXAMPLES_COMMON_OSAL_HOSTTIMEMICROS_H
#define SPUTTEROS_EXAMPLES_COMMON_OSAL_HOSTTIMEMICROS_H

/**
 * @file HostTimeMicros.h
 * @brief Shared host-native time source for example projects.
 *
 * Reads `std::chrono::steady_clock`, duration-casts to microseconds.
 * 64-bit µs wraps at ~584,942 years — effectively never.
 *
 * For an embedded variant, replace this with a platform-specific
 * implementation (e.g. `to_us_since_boot(get_absolute_time())` on RP2350).
 *
 * @author Ryan Massie (rmassie)
 * @date 4/16/2026
 */

#include "sputteros/osal/SputterTime.h"
#include <chrono>
#include <cstdint>

namespace ExamplesCommon
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

} // namespace ExamplesCommon

#endif // SPUTTEROS_EXAMPLES_COMMON_OSAL_HOSTTIMEMICROS_H
