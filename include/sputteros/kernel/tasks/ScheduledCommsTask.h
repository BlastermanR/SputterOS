#ifndef SPUTTEROS_KERNEL_TASKS_SCHEDULEDCOMMSTASK_H
#define SPUTTEROS_KERNEL_TASKS_SCHEDULEDCOMMSTASK_H

#include "sputteros/ConfigTraits.h"
#include "sputteros/comms/CLI.h"
#include "sputteros/comms/protocol/IProtocolHandler.h"
#include "sputteros/comms/protocol/MessageType.h"
#include "sputteros/comms/protocol/ResponseSerializer.h"
#include "sputteros/hal/devices/IStream.h"
#include "sputteros/kernel/KernelConstructTag.h"
#include "sputteros/osal/sync/ICommandProducer.h"
#include "sputteros/osal/tasks/IScheduledTask.h"

/**
 * @file ScheduledCommsTask.h
 * @brief Dual-mode kernel communications task — TEXT + FRAMED protocol bridge.
 *
 * In TEXT mode: reads ASCII lines from an `IStream`, feeds them through
 * a `CommandParser`, pushes validated commands into the `ICommandProducer`,
 * and sends ACK/NACK ASCII responses. This is backward-compatible behavior.
 *
 * In FRAMED mode: acts as `IProtocolHandler<Cfg>`, receiving dispatched
 * protocol messages from the `CLI` / `ProtocolRouter`. Commands are queued
 * with COBS-framed ACK/NACK responses. Metrics requests capture a
 * `PerformanceSnapshot` and send a METRICS_RESP frame. EXIT_HANDSHAKE
 * reverts the CLI to TEXT mode.
 *
 * The handshake mode switch is detected by the `CLI` (0x00 byte in TEXT
 * stream triggers a probe). This task provides the protocol handler that
 * the CLI dispatches to.
 *
 * @note This is a kernel task. Its constructor requires a `KernelConstructTag` —
 *       only `SystemBuilder` may instantiate it. The instance is owned by
 *       `System<Cfg>` as an `inline static` member.
 * @note Inherits `IScheduledTask` — pinned to Core 1 in multi-core configs.
 * @note Sends `NACK <cmd_id> <sub_id> <value>` on queue back-pressure (TEXT)
 *       or framed NACK (FRAMED).
 *
 * @tparam Cfg Configuration struct providing `Command`, `CmdID`, etc.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */
namespace SputterOS
{

template <typename Cfg> class SystemBuilder;

namespace Kernel
{

struct KernelTestAccess;

template <typename Cfg> class ScheduledCommsTask : public IScheduledTask, public IProtocolHandler<Cfg>
{
  public:
    /**
     * @brief Callback type for capturing a PerformanceSnapshot.
     *
     * Wired by SystemBuilder to `System<Cfg>::snapshot()`. Avoids
     * a circular include between ScheduledCommsTask and System.
     */
    using MetricsSnapshotFn = PerformanceSnapshot (*)();

    using CommandStruct = typename Cfg::Command;

    /**
     * @brief The task's activation period in microseconds.
     * @return Period from `CfgCommsBudgetUs<Cfg>`.
     */
    SputterMicros periodUs() const override { return CfgCommsBudgetUs<Cfg>::value; }

    /**
     * @brief Initialize the stream reader, wire up the protocol handler, and reset state.
     */
    void init() override
    {
        m_cli.setProtocolHandler(this);
        m_cli.tick();
    }

    /**
     * @brief Read available bytes, process commands, and send responses.
     * @param systemTimeMicros: Monotonic system time forwarded from the scheduler.
     *
     * Behavior adapts to the current `CommsMode` of the CLI:
     * - TEXT: drain parsed ASCII commands → queue → ACK/NACK ASCII
     * - FRAMED: drain framed commands → queue → ACK/NACK frames
     */
    void tick(SputterMicros /*systemTimeMicros*/) override
    {
        m_cli.tick();

        CommandStruct cmd{};
        while (m_cli.hasCommand())
        {
            if (m_cli.getCommand(cmd) && m_commandQueue)
            {
                if (m_commandQueue->try_push(cmd))
                {
                    if (m_cli.getMode() == CommsMode::FRAMED)
                    {
                        m_cli.sendFramedAck(m_cli.getLastFramedSeqNum(), static_cast<uint8_t>(cmd.id));
                    }
                    else
                    {
                        sendTextAck(cmd);
                    }
                }
                else
                {
                    if (m_cli.getMode() == CommsMode::FRAMED)
                    {
                        m_cli.sendFramedNack(m_cli.getLastFramedSeqNum(), static_cast<uint8_t>(cmd.id),
                                             cmd.targetDevice, cmd.value);
                    }
                    else
                    {
                        sendTextNack(cmd);
                    }
                }
            }
        }
    }

    /**
     * @brief Validate that all required injected dependencies are non-null.
     * @return true if every pointer is valid, false if any dependency is null.
     */
    bool validateDependencies() const override { return m_cli.hasStream() && m_commandQueue != nullptr; }

    /**
     * @brief PassKey constructor — only SystemBuilder and KernelTestAccess may instantiate.
     * @param tag: Opaque access token (see KernelConstructTag.h).
     * @param stream: Bidirectional byte stream (USB CDC, UART, etc.).
     * @param commandQueue: Producer-side queue into which parsed commands are pushed.
     */
    ScheduledCommsTask(KernelConstructTag /*tag*/, IStream *stream, ICommandProducer<Cfg> *commandQueue)
        : m_commandQueue(commandQueue), m_cli(stream)
    {
        // Intentionally Empty
    }

    /**
     * @brief Set the metrics snapshot callback.
     * @param fn  Function pointer to a PerformanceSnapshot capture routine.
     *
     * Called by SystemBuilder after construction to wire System<Cfg>::snapshot.
     */
    void setMetricsSnapshotFn(MetricsSnapshotFn fn) { m_snapshotFn = fn; }

    /**
     * @brief Access the underlying CLI for external telemetry output.
     * @return Reference to the internal CLI instance.
     */
    CLI<Cfg> &cli() { return m_cli; }

    // =====================================================================
    //  IProtocolHandler<Cfg> implementation (FRAMED mode callbacks)
    // =====================================================================

    /**
     * @brief Handle a decoded COMMAND frame.
     * @param cmd    Deserialized command struct.
     * @param seqNum Frame sequence number (used for ACK/NACK correlation).
     *
     * @note Commands from framed mode are stored in the CLI for the main
     *       `tick()` loop to pick up, so this callback is a no-op —
     *       the CLI stores the command directly in `handleDecodedFrame()`.
     */
    void onCommand(const CommandStruct & /*cmd*/, uint8_t /*seqNum*/) override
    {
        // Commands are handled in tick() via m_cli.hasCommand() / getCommand().
        // The CLI stores framed commands directly — no additional work needed.
    }

    /**
     * @brief Handle a HANDSHAKE_REQ frame.
     * @param version Protocol version from the host.
     * @param seqNum  Frame sequence number.
     *
     * Sends HANDSHAKE_RESP to confirm the mode switch.
     */
    void onHandshakeRequest(uint16_t /*version*/, uint8_t seqNum) override { m_cli.sendFramedHandshakeResp(seqNum); }

    /**
     * @brief Handle an EXIT_HANDSHAKE frame.
     * @param seqNum Frame sequence number.
     *
     * Sends a framed ACK and reverts the CLI to TEXT mode.
     */
    void onExitHandshake(uint8_t seqNum) override
    {
        m_cli.sendFramedAck(seqNum, static_cast<uint8_t>(MessageType::EXIT_HANDSHAKE));
        m_cli.exitFramedMode();
    }

    /**
     * @brief Handle a METRICS_REQ frame.
     * @param seqNum Frame sequence number.
     *
     * @note Metrics serialization requires access to System<Cfg>::snapshot(),
     *       which is wired externally. This base implementation sends an
     *       empty METRICS_RESP as a placeholder. Override or extend via
     *       a metrics callback for full snapshot serialization.
     */
    void onMetricsRequest(uint8_t seqNum) override
    {
        if (!m_snapshotFn)
        {
            m_cli.sendFramedMetricsResp(seqNum, nullptr, 0);
            return;
        }
        const PerformanceSnapshot snap = m_snapshotFn();
        uint8_t buf[ResponseSerializer::kMetricsHeaderSize + kMaxSnapshotTasks * ResponseSerializer::kMetricsTaskSize];
        const std::size_t len = ResponseSerializer::serializeMetrics(snap, buf, sizeof(buf));
        m_cli.sendFramedMetricsResp(seqNum, buf, len);
    }

    /**
     * @brief Handle a HEARTBEAT frame.
     * @param seqNum Frame sequence number.
     *
     * Echoes a heartbeat response.
     */
    void onHeartbeat(uint8_t seqNum) override { m_cli.sendFramedHeartbeat(seqNum); }

  private:
    friend class SystemBuilder<Cfg>;
    friend struct KernelTestAccess;

    /**
     * --------------------
     * Injected Dependencies
     * --------------------
     */
    ICommandProducer<Cfg> *m_commandQueue; /**< @brief Producer-side queue to ControlTask. */

    /**
     * --------------------
     * Metrics callback
     * --------------------
     */
    MetricsSnapshotFn m_snapshotFn = nullptr; /**< @brief Captures a PerformanceSnapshot (wired by builder). */

    /**
     * --------------------
     * CLI
     * --------------------
     */
    CLI<Cfg> m_cli; /**< @brief Dual-mode stream I/O, command parsing, and framed protocol. */

    /**
     * --------------------
     * TEXT Mode ACK / NACK Responses
     * --------------------
     */

    /**
     * @brief Send a TEXT-mode ACK response for a successfully queued command.
     * @param cmd: The command that was accepted.
     */
    void sendTextAck(const CommandStruct &cmd)
    {
        m_cli.builder().clear();
        m_cli.builder().append("ACK ").append(static_cast<int32_t>(cmd.id)).append('\n');
        m_cli.flush();
    }

    /**
     * @brief Send a TEXT-mode NACK response when the queue rejects a command.
     * @param cmd: The command that was dropped.
     *
     * Wire format: `NACK <cmd_id> <sub_id> <value>\n`
     */
    void sendTextNack(const CommandStruct &cmd)
    {
        m_cli.builder().clear();
        m_cli.builder()
            .append("NACK ")
            .append(static_cast<int32_t>(cmd.id))
            .append(' ')
            .append(static_cast<int32_t>(cmd.targetDevice))
            .append(' ')
            .append(cmd.value)
            .append('\n');
        m_cli.flush();
    }
};

} // namespace Kernel
} // namespace SputterOS

#endif // SPUTTEROS_KERNEL_TASKS_SCHEDULEDCOMMSTASK_H
