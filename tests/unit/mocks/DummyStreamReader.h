#ifndef SPUTTEROS_TESTS_MOCKS_DUMMYSTREAMREADER_H
#define SPUTTEROS_TESTS_MOCKS_DUMMYSTREAMREADER_H

#include "sputteros/hal/devices/IStream.h"
#include <cstddef>

/**
 * @file DummyStreamReader.h
 * @brief No-op stream reader that reports no data and drops all writes.
 *
 * Use this stub when no serial transport (USB CDC, UART, network) is
 * available or wired up yet. `CommsTask` will see an empty, disconnected
 * stream and simply idle each tick without pushing any commands.
 *
 * @warning This stub never produces or delivers bytes. Any telemetry
 *          written through it is silently discarded.
 * @warning Replace with a real implementation before connecting a host.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/6/2026
 */

namespace SputterOS
{

class DummyStreamReader : public IStream
{
  public:
    /**
     * @brief No bytes are ever available.
     * @return Always 0.
     */
    std::size_t available() const override { return 0; }

    /**
     * @brief Returns zero bytes — no data source is connected.
     * @param buffer: Unused.
     * @param max_len: Unused.
     * @return Always 0.
     */
    std::size_t read(uint8_t * /*buffer*/, std::size_t /*max_len*/) override { return 0; }

    /**
     * @brief Silently discards all write data.
     * @param data: Unused.
     * @param len: Number of bytes the caller intended to send.
     * @return Returns `len` so callers that check the return value do not stall.
     */
    std::size_t write(const uint8_t * /*data*/, std::size_t len) override { return len; }

    /**
     * @brief Always reports disconnected — no transport is present.
     * @return Always false.
     */
    bool isConnected() const override { return false; }
};

} // namespace SputterOS

#endif // SPUTTEROS_TESTS_MOCKS_DUMMYSTREAMREADER_H
