#ifndef SPUTTEROS_EXAMPLES_COMMON_HAL_ALWAYSSAFESAFETYMONITOR_H
#define SPUTTEROS_EXAMPLES_COMMON_HAL_ALWAYSSAFESAFETYMONITOR_H

/**
 * @file AlwaysSafeSafetyMonitor.h
 * @brief Shared stub ISafetyMonitor that is permanently safe.
 *
 * Used by all host-native example projects so that the kernel exercises
 * the full safety-evaluation path on every tick without tripping any
 * real condition.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/16/2026
 */

#include "sputteros/kernel/interfaces/ISafetyMonitor.h"

namespace ExamplesCommon
{

class AlwaysSafeSafetyMonitor : public SputterOS::ISafetyMonitor
{
  public:
    bool        isSafe() const override { return true; }
    const char *name() const override { return "AlwaysSafe"; }
};

} // namespace ExamplesCommon

#endif // SPUTTEROS_EXAMPLES_COMMON_HAL_ALWAYSSAFESAFETYMONITOR_H
