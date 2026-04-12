#ifndef SPUTTEROS_KERNEL_TASKS_SCHEDULEDCOMMSTASK_H
#define SPUTTEROS_KERNEL_TASKS_SCHEDULEDCOMMSTASK_H

#include "sputteros/ConfigTraits.h"
#include "sputteros/comms/CLI.h"
#include "sputteros/hal/devices/IStream.h"
#include "sputteros/kernel/KernelConstructTag.h"
#include "sputteros/osal/sync/ICommandProducer.h"
#include "sputteros/osal/tasks/IScheduledTask.h"

/**
 * @file ScheduledCommsTask.h
 * @brief Kernel communications task — stream reader to command queue bridge.
 *
 * Reads raw bytes from an `IStreamReader` (USB CDC, UART, or network),
 * feeds them through a `CommandParser`, and pushes validated command
 * packets into the shared `ICommandProducer` for the `ScheduledControlTask`
 * to consume via its `ICommandConsumer` view.
 *
 * Also responsible for writing telemetry responses back to the host via
 * the same `IStream` write path.
 *
 * @note This is a kernel task. Its constructor requires a `KernelConstructTag` —
 *       only `SystemBuilder` may instantiate it. The instance is owned by
 *       `System<Cfg>` as an `inline static` member.
 * @note Inherits `IScheduledTask` - pinned to Core 1 in multi-core configs.
 * @note Sends `NACK <cmd_id> <sub_id> <value>` on queue back-pressure.
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

template <typename Cfg> class ScheduledCommsTask : public IScheduledTask
{
  public:
    using CommandStruct = typename Cfg::Command;

    /**
     * @brief The task's activation period in microseconds.
     * @return Period from `CfgCommsBudgetUs<Cfg>`.
     */
    SputterMicros periodUs() const override { return CfgCommsBudgetUs<Cfg>::value; }

    /**
     * @brief Initialize the stream reader and reset the command parser.
     */
    void init() override { m_cli.tick(); }

    /**
     * @brief Read available bytes, feed the parser, and push any complete commands.
     * @param systemTimeMicros: Monotonic system time forwarded from the scheduler.
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
                    sendAck(cmd);
                }
                else
                {
                    sendNack(cmd);
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
     * CLI
     * --------------------
     */
    CLI<Cfg> m_cli; /**< @brief Stream I/O, command parsing, and telemetry formatter. */

    /**
     * --------------------
     * ACK / NACK Responses
     * --------------------
     */

    /**
     * @brief Send an ACK response to the host for a successfully queued command.
     * @param cmd: The command that was accepted.
     */
    void sendAck(const CommandStruct &cmd)
    {
        m_cli.builder().clear();
        m_cli.builder().append("ACK ").append(static_cast<int32_t>(cmd.id)).append('\n');
        m_cli.flush();
    }

    /**
     * @brief Send a NACK response to the host when the queue rejects a command.
     * @param cmd: The command that was dropped.
     *
     * Wire format: `NACK <cmd_id> <sub_id> <value>\n`
     */
    void sendNack(const CommandStruct &cmd)
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
