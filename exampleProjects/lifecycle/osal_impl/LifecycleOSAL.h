#ifndef LIFECYCLE_OSAL_IMPL_LIFECYCLEOSAL_H
#define LIFECYCLE_OSAL_IMPL_LIFECYCLEOSAL_H

/**
 * @file LifecycleOSAL.h
 * @brief OS-native platform utilities for the Lifecycle example.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "sputteros/osal/SputterTime.h"
#include <chrono>
#include <cstdint>

namespace Lifecycle
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

} // namespace Lifecycle

#endif // LIFECYCLE_OSAL_IMPL_LIFECYCLEOSAL_H
