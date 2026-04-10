#ifndef SPUTTEROS_LOGIC_COMMANDPARSER_H
#define SPUTTEROS_LOGIC_COMMANDPARSER_H

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "sputteros/ConfigTraits.h"

/**
 * @file CommandParser.h
 * @brief Stateful ASCII command parser for the CommsTask.
 *
 * Consumes a raw byte stream one character at a time and assembles
 * complete newline-terminated command strings into validated command
 * packets.
 *
 * Protocol format (ASCII, newline-terminated):
 * @code
 *   <CommandID> <targetDevice> <value>\n
 *   e.g. "1 0 50.0\n"  => SET_GAS_FLOW, device 0, 50.0 sccm
 * @endcode
 *
 * @tparam Cfg Configuration struct providing `Command`, `CmdID`, `kMaxValidCommandID`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */
namespace SputterOS
{

template <typename Cfg> class CommandParser
{
  public:
    using CommandStruct = typename Cfg::Command;
    using CommandID     = typename Cfg::CmdID;

    /**
     * @brief Construct a CommandParser with an empty parse buffer.
     */
    CommandParser() : m_len(0), m_pending{}, m_hasPending(false) {}

    /**
     * @brief Feed a single byte from the stream into the parser.
     * @param byte: Incoming byte to process.
     * @return true if a complete, valid command was assembled, false otherwise.
     *
     * When this returns true, call `getCommand()` to retrieve the result.
     * The parse buffer is automatically reset after a successful parse.
     */
    bool feedByte(uint8_t byte)
    {
        if (byte == '\n' || byte == '\r')
        {
            if (m_len > 0)
            {
                m_buf[m_len] = '\0';
                bool ok      = parseLine();
                m_len        = 0;
                return ok;
            }
            return false;
        }

        if (m_len >= kMaxLineLen - 1)
        {
            m_len = 0;
            return false;
        }

        m_buf[m_len++] = byte;
        return false;
    }

    /**
     * @brief Retrieve the most recently parsed command.
     * @param cmd: Reference populated with the parsed command on success.
     * @return true if a valid command is available, false if none pending.
     *
     * @note Calling this clears the pending command flag.
     */
    bool getCommand(CommandStruct &cmd)
    {
        if (!m_hasPending)
        {
            return false;
        }
        cmd          = m_pending;
        m_hasPending = false;
        return true;
    }

    /**
     * @brief Reset the internal parse buffer and discard any partial command.
     */
    void reset()
    {
        m_len        = 0;
        m_hasPending = false;
    }

  private:
    /**
     * --------------------
     * Parse State
     * --------------------
     */

    /** @brief Maximum number of characters in one command line (from Cfg or default 64). */
    static constexpr std::size_t kMaxLineLen = CfgMaxLineLen<Cfg>::value;

    uint8_t       m_buf[kMaxLineLen]; /**< @brief Accumulation buffer for incoming bytes. */
    std::size_t   m_len;              /**< @brief Number of bytes currently in the buffer. */
    CommandStruct m_pending;          /**< @brief Most recently parsed command. */
    bool          m_hasPending;       /**< @brief true when a valid command is waiting to be read. */

    /**
     * @brief Attempt to parse the contents of m_buf into m_pending.
     * @return true if parsing succeeded and m_pending is valid.
     */
    bool parseLine()
    {
        char *cursor = reinterpret_cast<char *>(m_buf);
        char *end    = nullptr;

        long rawId = std::strtol(cursor, &end, 10);
        if (end == cursor || !std::isspace(static_cast<unsigned char>(*end)))
        {
            return false;
        }
        cursor = end;

        long rawDevice = std::strtol(cursor, &end, 10);
        if (end == cursor || !std::isspace(static_cast<unsigned char>(*end)))
        {
            return false;
        }
        cursor = end;

        float rawValue = std::strtof(cursor, &end);
        if (end == cursor)
        {
            return false;
        }

        if (rawId < 0 || rawId > static_cast<long>(CfgMaxValidCommandID<Cfg>::value))
        {
            return false;
        }

        m_pending.id           = static_cast<CommandID>(rawId);
        m_pending.targetDevice = static_cast<uint8_t>(rawDevice);
        m_pending.value        = rawValue;
        m_hasPending           = true;
        return true;
    }
};

} // namespace SputterOS

#endif // SPUTTEROS_LOGIC_COMMANDPARSER_H
