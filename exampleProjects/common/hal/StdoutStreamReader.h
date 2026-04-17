#ifndef SPUTTEROS_EXAMPLES_COMMON_HAL_STDOUTSTREAMREADER_H
#define SPUTTEROS_EXAMPLES_COMMON_HAL_STDOUTSTREAMREADER_H

/**
 * @file StdoutStreamReader.h
 * @brief Shared stub IStream that sinks input and writes output to stdout.
 *
 * Used by all host-native example projects.  Because `available()` always
 * returns 0, the CLI parser receives no bytes and no commands are ever
 * enqueued.  Telemetry written via `TelemetryLogger::drain()` is forwarded
 * to the host terminal through `write()`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/16/2026
 */

#include "sputteros/hal/devices/IStream.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace ExamplesCommon
{

class StdoutStreamReader : public SputterOS::IStream
{
  public:
    std::size_t available() const override { return 0; }
    std::size_t read(uint8_t * /*buffer*/, std::size_t /*max_len*/) override { return 0; }
    std::size_t write(const uint8_t *data, std::size_t len) override { return std::fwrite(data, 1, len, stdout); }
    bool        isConnected() const override { return true; }
};

} // namespace ExamplesCommon

#endif // SPUTTEROS_EXAMPLES_COMMON_HAL_STDOUTSTREAMREADER_H
