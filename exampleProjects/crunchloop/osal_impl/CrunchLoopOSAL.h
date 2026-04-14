#ifndef CRUNCHLOOP_OSAL_IMPL_CRUNCHLOOPOSAL_H
#define CRUNCHLOOP_OSAL_IMPL_CRUNCHLOOPOSAL_H

/**
 * @file CrunchLoopOSAL.h
 * @brief OS-native platform utilities for the CrunchLoop example.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/14/2026
 */

#include "sputteros/osal/SputterTime.h"
#include <chrono>
#include <cstdint>

namespace CrunchLoop
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

} // namespace CrunchLoop

#endif // CRUNCHLOOP_OSAL_IMPL_CRUNCHLOOPOSAL_H
