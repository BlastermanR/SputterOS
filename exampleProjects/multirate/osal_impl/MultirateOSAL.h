#ifndef MULTIRATE_OSAL_IMPL_MULTIRATEOSAL_H
#define MULTIRATE_OSAL_IMPL_MULTIRATEOSAL_H

/**
 * @file MultirateOSAL.h
 * @brief OS-native platform utilities for the MultiRate example.
 *
 * Provides `platformGetTimeMicros()` backed by
 * `std::chrono::steady_clock`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "sputteros/osal/SputterTime.h"
#include <chrono>
#include <cstdint>

namespace Multirate
{

/**
 * @brief Return the current monotonic time as a `SputterMicros` value.
 *
 * @return Current time in microseconds.
 */
inline SputterOS::SputterMicros platformGetTimeMicros()
{
    using namespace std::chrono;
    return static_cast<SputterOS::SputterMicros>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

} // namespace Multirate

#endif // MULTIRATE_OSAL_IMPL_MULTIRATEOSAL_H
