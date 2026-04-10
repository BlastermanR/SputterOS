#ifndef SPUTTEROS_UNIT_MOCKS_MOCKSTREAMREADER_H
#define SPUTTEROS_UNIT_MOCKS_MOCKSTREAMREADER_H

#include "sputteros/hal/devices/IStream.h"
#include <gmock/gmock.h>

/**
 * @file MockStreamReader.h
 * @brief GoogleMock implementation of IStream for unit testing.
 *
 * Inject into `CommsTask` and `CLI` test fixtures. Use `EXPECT_CALL` with
 * `SetArrayArgument` or `DoAll` + `Return` to feed synthetic byte sequences
 * into the parser and verify that correct commands are produced and that
 * telemetry responses are written back to the stream.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/5/2026
 */

namespace SputterOS
{

class MockStreamReader : public IStream
{
  public:
    MOCK_METHOD(std::size_t, available, (), (const, override));
    MOCK_METHOD(std::size_t, read, (uint8_t *buffer, std::size_t max_len), (override));
    MOCK_METHOD(std::size_t, write, (const uint8_t *data, std::size_t len), (override));
    MOCK_METHOD(bool, isConnected, (), (const, override));
};

} // namespace SputterOS

#endif // SPUTTEROS_UNIT_MOCKS_MOCKSTREAMREADER_H
