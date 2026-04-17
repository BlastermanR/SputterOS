#ifndef SPUTTEROS_OSAL_ICOMMANDCONSUMER_H
#define SPUTTEROS_OSAL_ICOMMANDCONSUMER_H

/**
 * @file ICommandConsumer.h
 * @brief Read-only command queue interface for consumer tasks.
 *
 * `ICommandConsumer` exposes only the `try_pop()` operation, enforcing
 * a strict directional contract: the holder of this interface can only
 * dequeue commands — never enqueue them.
 *
 * `ControlTask` receives an `ICommandConsumer*` to drain pending commands
 * from the shared queue without gaining access to the producer side.
 *
 * @tparam Cfg Configuration struct providing `Cfg::Command`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */
namespace SputterOS
{

template <typename Cfg> class ICommandConsumer
{
  public:
    using CommandStruct = typename Cfg::Command;

    /**
     * @brief Virtual destructor.
     */
    virtual ~ICommandConsumer() = default;

    /**
     * @brief Attempt to pop a command without blocking.
     * @param cmd: Reference populated with the dequeued command.
     * @return true if a command was dequeued, false if the queue is empty.
     */
    virtual bool try_pop(CommandStruct &cmd) = 0;

    ICommandConsumer(const ICommandConsumer &)            = delete;
    ICommandConsumer &operator=(const ICommandConsumer &) = delete;

  protected:
    ICommandConsumer() = default;
};

} // namespace SputterOS

#endif // SPUTTEROS_OSAL_ICOMMANDCONSUMER_H
