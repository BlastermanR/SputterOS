#ifndef LIFECYCLE_HAL_IMPL_LIFECYCLEHAL_H
#define LIFECYCLE_HAL_IMPL_LIFECYCLEHAL_H

/**
 * @file LifecycleHAL.h
 * @brief Stub HAL implementations for the Lifecycle example.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "sputteros/hal/devices/IStream.h"
#include "sputteros/kernel/interfaces/ISafetyMonitor.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace Lifecycle
{

/**
 * @brief Stream that sinks input to /dev/null and writes output to stdout.
 */
class StdoutStreamReader : public SputterOS::IStream
{
  public:
    std::size_t available() const override { return 0; }
    std::size_t read(uint8_t * /*buffer*/, std::size_t /*max_len*/) override { return 0; }
    std::size_t write(const uint8_t *data, std::size_t len) override { return std::fwrite(data, 1, len, stdout); }
    bool        isConnected() const override { return true; }
};

/**
 * @brief Safety monitor that is permanently safe.
 */
class AlwaysSafeSafetyMonitor : public SputterOS::ISafetyMonitor
{
  public:
    bool        isSafe() const override { return true; }
    const char *name() const override { return "AlwaysSafe"; }
};

} // namespace Lifecycle

#endif // LIFECYCLE_HAL_IMPL_LIFECYCLEHAL_H
