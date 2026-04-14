#ifndef PINGPONG_HAL_IMPL_PINGPONGHAL_H
#define PINGPONG_HAL_IMPL_PINGPONGHAL_H

/**
 * @file PingPongHAL.h
 * @brief Null and stub HAL implementations for the PingPong dual-core example.
 *
 * The PingPong example exercises the full dual-core SputterOS kernel
 * without requiring any real hardware.  Each class below satisfies a
 * SputterOS interface contract with the safest possible no-op behaviour.
 *
 * ### Classes
 * - `StdoutStreamReader`       — Zero input, writes telemetry to stdout;
 *                                satisfies `IStream`.
 * - `AlwaysSafeSafetyMonitor`  — Always reports safe; satisfies `ISafetyMonitor`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "sputteros/hal/devices/IStream.h"
#include "sputteros/kernel/interfaces/ISafetyMonitor.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace PingPong
{

// =============================================================================
// StdoutStreamReader
// =============================================================================

/**
 * @brief Stream reader that sinks incoming commands to /dev/null and
 *        forwards outgoing telemetry bytes directly to stdout.
 *
 * Passed to `SystemBuilder::setStream()` as the "serial port".  Because
 * `available()` always returns 0, the CLI parser receives no bytes and
 * no commands are ever enqueued.
 */
class StdoutStreamReader : public SputterOS::IStream
{
  public:
    std::size_t available() const override { return 0; }
    std::size_t read(uint8_t * /*buffer*/, std::size_t /*max_len*/) override { return 0; }
    std::size_t write(const uint8_t *data, std::size_t len) override { return std::fwrite(data, 1, len, stdout); }
    bool        isConnected() const override { return true; }
};

// =============================================================================
// AlwaysSafeSafetyMonitor
// =============================================================================

/**
 * @brief Safety monitor that is permanently safe.
 *
 * Passed to `SystemBuilder` so that the kernel exercises the full
 * safety-evaluation path on every tick without tripping any real condition.
 */
class AlwaysSafeSafetyMonitor : public SputterOS::ISafetyMonitor
{
  public:
    bool        isSafe() const override { return true; }
    const char *name() const override { return "AlwaysSafe"; }
};

} // namespace PingPong

#endif // PINGPONG_HAL_IMPL_PINGPONGHAL_H
