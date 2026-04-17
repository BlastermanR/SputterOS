#ifndef SPUTTEROS_HAL_INTERFACES_ISTREAM_H
#define SPUTTEROS_HAL_INTERFACES_ISTREAM_H

#include <cstddef>
#include <cstdint>

/**
 * @file IStream.h
 * @brief Interface for bidirectional byte-stream I/O (USB, UART, Network).
 *
 * Abstracts serial communication channels so `CommsTask` can operate
 * identically over USB CDC, UART, or network transports.
 *
 * @note Implementations must be non-blocking; `read()` must return
 *       immediately with whatever bytes are currently available.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/5/2026
 */
namespace SputterOS
{

class IStream
{
  public:
    /**
     * @brief Virtual destructor.
     */
    virtual ~IStream() = default;

    /**
     * @brief Return how many bytes are available without blocking.
     * @return Number of bytes ready in the receive buffer.
     */
    virtual std::size_t available() const = 0;

    /**
     * @brief Read bytes non-blocking into a caller-supplied buffer.
     * @param buffer: Destination buffer for incoming bytes.
     * @param max_len: Maximum number of bytes to read.
     * @return Number of bytes actually read (0 if none available).
     */
    virtual std::size_t read(uint8_t *buffer, std::size_t max_len) = 0;

    /**
     * @brief Write bytes to the stream for telemetry responses.
     * @param data: Source buffer to transmit.
     * @param len: Number of bytes to send.
     * @return Number of bytes actually written.
     */
    virtual std::size_t write(const uint8_t *data, std::size_t len) = 0;

    /**
     * @brief Check whether the stream is connected and operational.
     * @return true if the transport link is active, false otherwise.
     */
    virtual bool isConnected() const = 0;

    // Non-copyable — implementations own hardware resources.
    IStream(const IStream &)            = delete;
    IStream &operator=(const IStream &) = delete;

  protected:
    IStream() = default;
};

} // namespace SputterOS

#endif // SPUTTEROS_HAL_INTERFACES_ISTREAM_H
