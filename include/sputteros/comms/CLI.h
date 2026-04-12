#ifndef SPUTTEROS_COMMS_CLI_H
#define SPUTTEROS_COMMS_CLI_H

#include "sputteros/comms/protocol/CommsMode.h"
#include "sputteros/comms/protocol/FrameConstants.h"
#include "sputteros/comms/protocol/FrameDecoder.h"
#include "sputteros/comms/protocol/FrameEncoder.h"
#include "sputteros/comms/protocol/IProtocolHandler.h"
#include "sputteros/comms/protocol/MessageType.h"
#include "sputteros/comms/protocol/ProtocolRouter.h"
#include "sputteros/comms/protocol/ResponseSerializer.h"
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
 * @brief Dual-mode command-line interface: TEXT (ASCII) + FRAMED (COBS binary).
 *
 * Wraps `IStream`, `CommandParser`, `FrameDecoder`, `FrameEncoder`,
 * `ProtocolRouter`, and `LightweightStringBuilder` into a single object
 * used by `CommsTask`.
 *
 * The CLI starts in TEXT mode (backward-compatible ASCII line protocol).
 * A host tool can negotiate FRAMED mode by sending a COBS-encoded
 * HANDSHAKE_REQ frame. Since 0x00 never appears in valid ASCII data,
 * the CLI detects the delimiter byte as a handshake probe trigger.
 *
 * In FRAMED mode:
 * - Incoming bytes are fed to a FrameDecoder.
 * - Complete frames are dispatched via ProtocolRouter to an IProtocolHandler.
 * - Outgoing messages (ACK, NACK, data, metrics) are COBS-framed.
 * - EXIT_HANDSHAKE reverts to TEXT mode.
 *
 * @note The `CLI` borrows the `IStream` pointer; ownership stays with
 *       the caller.
 *
 * @tparam Cfg  Configuration struct providing `Command`, `CmdID`, `kMaxValidCommandID`.
 * @tparam MaxPayload Maximum payload size for framed protocol (default: 256).
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */
template <typename Cfg, std::size_t MaxPayload = 256> class CLI
{
  public:
    using CommandStruct = typename Cfg::Command;

    /**
     * @brief Construct a CLI that reads from and writes to the given stream.
     * @param stream: Bidirectional byte stream (USB CDC, UART, etc.).
     *        The CLI borrows this pointer and does not take ownership.
     */
    explicit CLI(IStream *stream) : m_stream(stream), m_hasPending(false), m_mode(CommsMode::TEXT),
                                    m_hasFramedCommand(false), m_framedSeqNum(0),
                                    m_handler(nullptr), m_router(nullptr),
                                    m_probeActive(false), m_probeLen(0) {}

    /**
     * @brief Query whether the CLI has a valid (non-null) stream.
     * @return true if the stream pointer is non-null.
     */
    bool hasStream() const { return m_stream != nullptr; }

    /**
     * @brief Get the current communications mode.
     * @return CommsMode::TEXT or CommsMode::FRAMED.
     */
    CommsMode getMode() const { return m_mode; }

    /**
     * @brief Set the IProtocolHandler for framed-mode message dispatch.
     * @param handler Borrowed pointer to the protocol handler.
     *
     * Must be set before framed mode is entered. If null, framed mode
     * will not accept the handshake.
     */
    void setProtocolHandler(IProtocolHandler<Cfg> *handler)
    {
        m_handler = handler;
        if (m_handler)
        {
            new (&m_routerStorage) ProtocolRouter<Cfg>(m_handler);
            m_router = reinterpret_cast<ProtocolRouter<Cfg> *>(&m_routerStorage);
        }
        else
        {
            m_router = nullptr;
        }
    }

    /**
     * @brief Drain available bytes from the stream and process them.
     *
     * In TEXT mode: feeds bytes to CommandParser (+ handshake probe on 0x00).
     * In FRAMED mode: feeds bytes to FrameDecoder, dispatches via ProtocolRouter.
     *
     * Call once per `CommsTask::tick()`.
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

        if (m_mode == CommsMode::TEXT)
        {
            processTextBytes(got);
        }
        else
        {
            processFramedBytes(got);
        }
    }

    /**
     * @brief Check whether a complete, validated command is ready.
     * @return true if a command can be retrieved via `getCommand()`.
     */
    bool hasCommand() const
    {
        if (m_mode == CommsMode::TEXT)
        {
            return m_hasPending;
        }
        return m_hasFramedCommand;
    }

    /**
     * @brief Retrieve the pending command and clear the ready flag.
     * @param cmd: Reference populated with the parsed command on success.
     * @return true if a command was available, false otherwise.
     */
    bool getCommand(CommandStruct &cmd)
    {
        if (m_mode == CommsMode::TEXT)
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

        if (!m_hasFramedCommand)
        {
            return false;
        }
        cmd              = m_framedCommand;
        m_hasFramedCommand = false;
        return true;
    }

    /**
     * @brief Get the sequence number of the last framed command (for ACK/NACK).
     * @return Sequence number from the most recent COMMAND frame.
     */
    uint8_t getLastFramedSeqNum() const { return m_framedSeqNum; }

    /**
     * @brief Access the internal telemetry string builder.
     * @return Reference to the `LightweightStringBuilder` used for TX.
     *
     * Call `flush()` after building the string to transmit it.
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

    // =====================================================================
    //  Framed-mode output helpers
    // =====================================================================

    /**
     * @brief Send a framed ACK message.
     * @param seqNum Sequence number to echo.
     * @param cmdId  Command ID being acknowledged.
     */
    void sendFramedAck(uint8_t seqNum, uint8_t cmdId)
    {
        uint8_t payload[1];
        std::size_t pLen = ResponseSerializer::serializeAck(cmdId, payload, sizeof(payload));
        sendFrame(MessageType::ACK, seqNum, payload, pLen);
    }

    /**
     * @brief Send a framed NACK message.
     * @param seqNum       Sequence number to echo.
     * @param cmdId        Command ID being rejected.
     * @param targetDevice Target device.
     * @param value        Command value.
     */
    void sendFramedNack(uint8_t seqNum, uint8_t cmdId, uint8_t targetDevice, float value)
    {
        uint8_t payload[6];
        std::size_t pLen = ResponseSerializer::serializeNack(cmdId, targetDevice, value, payload, sizeof(payload));
        sendFrame(MessageType::NACK, seqNum, payload, pLen);
    }

    /**
     * @brief Send a framed HANDSHAKE_RESP message.
     * @param seqNum Sequence number to echo.
     */
    void sendFramedHandshakeResp(uint8_t seqNum)
    {
        uint8_t payload[6];
        std::size_t pLen = ResponseSerializer::serializeHandshakeResp(payload, sizeof(payload));
        sendFrame(MessageType::HANDSHAKE_RESP, seqNum, payload, pLen);
    }

    /**
     * @brief Send a framed HEARTBEAT message.
     * @param seqNum Sequence number to echo.
     */
    void sendFramedHeartbeat(uint8_t seqNum) { sendFrame(MessageType::HEARTBEAT, seqNum, nullptr, 0); }

    /**
     * @brief Send a framed DATA message with user-defined payload.
     * @param payload Pointer to data bytes.
     * @param len     Number of bytes.
     */
    void sendFramedData(const uint8_t *payload, std::size_t len)
    {
        sendFrame(MessageType::DATA, 0, payload, len);
    }

    /**
     * @brief Send a framed LOG message.
     * @param level Verbosity level.
     * @param text  Null-terminated log string.
     */
    void sendFramedLog(uint8_t level, const char *text)
    {
        uint8_t payload[MaxPayload];
        std::size_t pLen = ResponseSerializer::serializeLog(level, text, payload, sizeof(payload));
        if (pLen > 0)
        {
            sendFrame(MessageType::LOG, 0, payload, pLen);
        }
    }

    /**
     * @brief Send raw bytes as a METRICS_RESP frame.
     * @param seqNum Sequence number to echo.
     * @param data   Pointer to serialized metrics data.
     * @param len    Number of bytes.
     */
    void sendFramedMetricsResp(uint8_t seqNum, const uint8_t *data, std::size_t len)
    {
        sendFrame(MessageType::METRICS_RESP, seqNum, data, len);
    }

    /**
     * @brief Switch from TEXT → FRAMED mode.
     *
     * Called internally by the handshake detection logic. Can also be
     * called externally if the application wants to force framed mode.
     */
    void enterFramedMode()
    {
        m_mode        = CommsMode::FRAMED;
        m_probeActive = false;
        m_probeLen    = 0;
        m_frameDecoder.reset();
    }

    /**
     * @brief Switch from FRAMED → TEXT mode.
     *
     * Called when EXIT_HANDSHAKE is received or on timeout.
     */
    void exitFramedMode()
    {
        m_mode           = CommsMode::TEXT;
        m_hasPending     = false;
        m_hasFramedCommand = false;
        m_parser.reset();
    }

  private:
    IStream                  *m_stream;     /**< @brief Borrowed stream reference. */
    CommandParser<Cfg>        m_parser;     /**< @brief Stateful byte-stream parser (TEXT mode). */
    LightweightStringBuilder  m_builder;    /**< @brief TX telemetry buffer. */
    bool                      m_hasPending; /**< @brief Set when parser produces a command. */

    /** @brief Maximum bytes drained from the stream per `tick()`. */
    static constexpr std::size_t kReadBufSize = 64;

    uint8_t m_readBuf[kReadBufSize]; /**< @brief Scratch buffer for incoming bytes. */

    // ── Dual-mode state ──────────────────────────────────────────────────
    CommsMode m_mode;                                /**< @brief Current protocol mode. */
    FrameDecoder<MaxPayload> m_frameDecoder;         /**< @brief COBS frame decoder (FRAMED mode). */
    FrameEncoder<MaxPayload> m_frameEncoder;         /**< @brief COBS frame encoder (output). */

    CommandStruct m_framedCommand;                   /**< @brief Last decoded framed command. */
    bool          m_hasFramedCommand;                /**< @brief A framed command is pending. */
    uint8_t       m_framedSeqNum;                    /**< @brief SeqNum of last framed command. */

    IProtocolHandler<Cfg>   *m_handler;              /**< @brief Borrowed protocol handler. */
    ProtocolRouter<Cfg>     *m_router;               /**< @brief Points to m_routerStorage when active. */

    /** @brief Placement storage for the ProtocolRouter. */
    alignas(ProtocolRouter<Cfg>) char m_routerStorage[sizeof(ProtocolRouter<Cfg>)];

    // ── Handshake probe state (TEXT mode) ────────────────────────────────
    static constexpr std::size_t kMaxProbeSize = 64;
    uint8_t     m_probeBuf[kMaxProbeSize];           /**< @brief Accumulates COBS bytes for probe. */
    bool        m_probeActive;                       /**< @brief True after seeing a 0x00 sync byte. */
    std::size_t m_probeLen;                          /**< @brief Current probe buffer position. */

    // ── TEXT mode byte processing ────────────────────────────────────────

    /**
     * @brief Process bytes in TEXT mode with handshake probe detection.
     * @param count Number of bytes in m_readBuf to process.
     *
     * Detection protocol: the host sends a sync 0x00 before the COBS-
     * encoded handshake frame, producing `[0x00][COBS data][0x00]`.
     * Since 0x00 never appears in valid ASCII, the first 0x00 triggers
     * accumulation; the second 0x00 triggers a decode attempt.
     */
    void processTextBytes(std::size_t count)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            const uint8_t b = m_readBuf[i];

            if (b == kFrameDelimiter)
            {
                // 0x00 never appears in ASCII — this is a COBS delimiter.
                if (m_probeActive && m_probeLen > 0)
                {
                    if (tryHandshakeProbe())
                    {
                        // Successful handshake — now in FRAMED mode.
                        // Feed remaining bytes to framed processor.
                        processFramedBytesFrom(m_readBuf, i + 1, count);
                        return;
                    }
                }
                // Start/restart probe: accumulate bytes until next 0x00.
                m_probeActive = true;
                m_probeLen    = 0;
                continue;
            }

            // If we're in probe mode (seen a 0x00), accumulate bytes.
            if (m_probeActive)
            {
                if (m_probeLen < kMaxProbeSize)
                {
                    m_probeBuf[m_probeLen++] = b;
                }
                else
                {
                    // Probe overflow — not a valid COBS frame.
                    m_probeActive = false;
                    m_probeLen    = 0;
                    // Feed this byte to text parser.
                    if (m_parser.feedByte(b))
                    {
                        m_hasPending = true;
                    }
                }
                continue;
            }

            // Normal ASCII byte — feed the text parser.
            if (m_parser.feedByte(b))
            {
                m_hasPending = true;
            }
        }
    }

    /**
     * @brief Attempt to decode the probe buffer as a HANDSHAKE_REQ frame.
     * @return true if handshake was successful and mode switched to FRAMED.
     */
    bool tryHandshakeProbe()
    {
        if (!m_handler || !m_router)
        {
            return false;
        }

        // Try to decode the probe buffer as a COBS frame.
        FrameDecoder<MaxPayload> probeDec;
        DecoderResult result = DecoderResult::INCOMPLETE;

        for (std::size_t i = 0; i < m_probeLen; ++i)
        {
            result = probeDec.feedByte(m_probeBuf[i]);
        }
        // Feed the delimiter (0x00) that triggered this call.
        result = probeDec.feedByte(kFrameDelimiter);

        if (result != DecoderResult::FRAME_READY)
        {
            return false;
        }

        // Only accept HANDSHAKE_REQ as a mode-switch trigger.
        if (probeDec.getMessageType() != MessageType::HANDSHAKE_REQ)
        {
            return false;
        }

        // Dispatch through the router (validates magic, calls handler).
        if (!m_router->dispatch(probeDec.getMessageType(), probeDec.getSeqNum(),
                                probeDec.getPayload(), probeDec.getPayloadLen()))
        {
            return false;
        }

        enterFramedMode();
        return true;
    }

    // ── FRAMED mode byte processing ─────────────────────────────────────

    /**
     * @brief Process bytes in FRAMED mode.
     * @param count Number of bytes in m_readBuf to process.
     */
    void processFramedBytes(std::size_t count)
    {
        processFramedBytesFrom(m_readBuf, 0, count);
    }

    /**
     * @brief Process bytes from a buffer starting at an offset.
     * @param buf   Source buffer.
     * @param start Start index.
     * @param end   End index (exclusive).
     */
    void processFramedBytesFrom(const uint8_t *buf, std::size_t start, std::size_t end)
    {
        for (std::size_t i = start; i < end; ++i)
        {
            const auto result = m_frameDecoder.feedByte(buf[i]);

            if (result == DecoderResult::FRAME_READY)
            {
                handleDecodedFrame();
            }
        }
    }

    /**
     * @brief Process a successfully decoded frame.
     */
    void handleDecodedFrame()
    {
        const MessageType type = m_frameDecoder.getMessageType();
        const uint8_t seq      = m_frameDecoder.getSeqNum();

        // COMMAND frames are stored for the CommsTask to pick up.
        if (type == MessageType::COMMAND)
        {
            // Deserialize the command payload directly.
            if (m_frameDecoder.getPayloadLen() == 6)
            {
                const uint8_t *p = m_frameDecoder.getPayload();
                m_framedCommand.id           = static_cast<typename Cfg::CmdID>(p[0]);
                m_framedCommand.targetDevice = p[1];
                std::memcpy(&m_framedCommand.value, &p[2], sizeof(float));
                m_hasFramedCommand = true;
                m_framedSeqNum     = seq;
            }
            return;
        }

        // All other message types are dispatched to the handler.
        if (m_router)
        {
            m_router->dispatch(type, seq, m_frameDecoder.getPayload(), m_frameDecoder.getPayloadLen());
        }
    }

    // ── Frame output ─────────────────────────────────────────────────────

    /**
     * @brief Encode and transmit a framed message.
     * @param type    Message type.
     * @param seqNum  Sequence number.
     * @param payload Payload bytes (may be nullptr if len == 0).
     * @param len     Payload length.
     */
    void sendFrame(MessageType type, uint8_t seqNum, const uint8_t *payload, std::size_t len)
    {
        if (!m_stream)
        {
            return;
        }
        uint8_t wire[FrameEncoder<MaxPayload>::kMaxWireSize];
        std::size_t n = m_frameEncoder.encode(type, seqNum, payload, len, wire, sizeof(wire));
        if (n > 0)
        {
            m_stream->write(wire, n);
        }
    }
};

} // namespace SputterOS

#endif // SPUTTEROS_COMMS_CLI_H
