#ifndef SPUTTEROS_COMMS_CLI_H
#define SPUTTEROS_COMMS_CLI_H

#include "sputteros/hal/devices/IStream.h"
#include "sputteros/logic/CommandParser.h"
#include "sputteros/utils/logging/LightweightStringBuilder.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace SputterOS
{

/**
 * @file CLI.h
 * @brief Command-line interface combining stream I/O, parsing, and telemetry.
 *
 * Wraps `IStream`, `CommandParser`, and `LightweightStringBuilder`
 * into a single object used by `CommsTask`. Responsibilities:
 *
 * - Drain the `IStream` receive buffer each tick().
 * - Feed incoming bytes through the internal `CommandParser`.
 * - Expose complete validated commands via `hasCommand()` / `getCommand()`.
 * - Provide `builder()` + `flush()` for formatting and transmitting
 *   telemetry responses back to the host.
 *
 * @note The `CLI` borrows the `IStream` pointer; ownership stays with
 *       the caller.
 *
 * @tparam Cfg Configuration struct providing `Command`, `CmdID`, `kMaxValidCommandID`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */
template <typename Cfg> class CLI
{
  public:
    using CommandStruct = typename Cfg::Command;

    /**
     * @brief Construct a CLI that reads from and writes to the given stream.
     * @param stream: Bidirectional byte stream (USB CDC, UART, etc.).
     *        The CLI borrows this pointer and does not take ownership.
     */
    explicit CLI(IStream *stream) : m_stream(stream), m_hasPending(false) {}

    /**
     * @brief Query whether the CLI has a valid (non-null) stream.
     * @return true if the stream pointer is non-null.
     */
    bool hasStream() const { return m_stream != nullptr; }

    /**
     * @brief Drain available bytes from the stream and feed the parser.
     *
     * Reads up to `kReadBufSize` bytes non-blocking per call and submits
     * each byte to the internal `CommandParser`. Call once per
     * `CommsTask::tick()`.
     */
    void tick()
    {
        if (!m_stream)
        {
            return;
        }

        std::size_t avail  = m_stream->available();
        std::size_t toRead = (avail < kReadBufSize) ? avail : kReadBufSize;
        if (toRead == 0)
        {
            return;
        }

        std::size_t got = m_stream->read(m_readBuf, toRead);
        for (std::size_t i = 0; i < got; ++i)
        {
            if (m_parser.feedByte(m_readBuf[i]))
            {
                m_hasPending = true;
            }
        }
    }

    /**
     * @brief Check whether a complete, validated command is ready.
     * @return true if a command can be retrieved via `getCommand()`.
     */
    bool hasCommand() const { return m_hasPending; }

    /**
     * @brief Retrieve the pending command and clear the ready flag.
     * @param cmd: Reference populated with the parsed command on success.
     * @return true if a command was available, false otherwise.
     */
    bool getCommand(CommandStruct &cmd)
    {
        if (!m_hasPending)
        {
            return false;
        }
        const bool ok = m_parser.getCommand(cmd);
        if (ok)
        {
            m_hasPending = false;
        }
        return ok;
    }

    /**
     * @brief Access the internal telemetry string builder.
     * @return Reference to the `LightweightStringBuilder` used for TX.
     *
     * Call `flush()` after building the string to transmit it.
     *
     * @code
     *   cli.builder().clear()
     *      .append("STATE:").append(int32_t(state))
     *      .append(",P:").append(pressure, 3)
     *      .append("\n");
     *   cli.flush();
     * @endcode
     */
    LightweightStringBuilder &builder() { return m_builder; }

    /**
     * @brief Write the builder contents to the stream, then clear the builder.
     *
     * No-op if the stream reports disconnected or the builder is empty.
     */
    void flush()
    {
        if (!m_stream || m_builder.length() == 0)
        {
            return;
        }
        m_stream->write(reinterpret_cast<const uint8_t *>(m_builder.c_str()), m_builder.length());
        m_builder.clear();
    }

    /**
     * @brief Write a null-terminated string directly to the stream.
     * @param str: String to transmit.
     */
    void print(const char *str)
    {
        if (!m_stream || !str)
        {
            return;
        }
        m_stream->write(reinterpret_cast<const uint8_t *>(str), std::strlen(str));
    }

    /**
     * @brief Write a null-terminated string followed by '\\n' to the stream.
     * @param str: String to transmit.
     */
    void println(const char *str)
    {
        print(str);
        static const uint8_t nl = '\n';
        if (m_stream)
        {
            m_stream->write(&nl, 1);
        }
    }

  private:
    IStream                 *m_stream;     /**< @brief Borrowed stream reference. */
    CommandParser<Cfg>       m_parser;     /**< @brief Stateful byte-stream parser. */
    LightweightStringBuilder m_builder;    /**< @brief TX telemetry buffer. */
    bool                     m_hasPending; /**< @brief Set when parser produces a command. */

    /** @brief Maximum bytes drained from the stream per `tick()`. */
    static constexpr std::size_t kReadBufSize = 64;

    uint8_t m_readBuf[kReadBufSize]; /**< @brief Scratch buffer for incoming bytes. */
};

} // namespace SputterOS

#endif // SPUTTEROS_COMMS_CLI_H
