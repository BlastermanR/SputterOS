#ifndef SPUTTEROS_OSAL_ICOMMANDPRODUCER_H
#define SPUTTEROS_OSAL_ICOMMANDPRODUCER_H

/**
 * @file ICommandProducer.h
 * @brief Write-only command queue interface for producer tasks.
 *
 * `ICommandProducer` exposes only the `try_push()` operation, enforcing
 * a strict directional contract: the holder of this interface can only
 * enqueue commands — never dequeue them.
 *
 * `CommsTask` receives an `ICommandProducer*` to push parsed commands
 * into the shared queue without gaining access to the consumer side.
 *
 * @tparam Cfg Configuration struct providing `Cfg::Command`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */
namespace SputterOS
{

template <typename Cfg> class ICommandProducer
{
  public:
    using CommandStruct = typename Cfg::Command;

    /**
     * @brief Virtual destructor.
     */
    virtual ~ICommandProducer() = default;

    /**
     * @brief Attempt to push a command without blocking.
     * @param cmd: Command to enqueue.
     * @return true if the command was accepted, false if the queue is full.
     */
    virtual bool try_push(const CommandStruct &cmd) = 0;

    ICommandProducer(const ICommandProducer &)            = delete;
    ICommandProducer &operator=(const ICommandProducer &) = delete;

  protected:
    ICommandProducer() = default;
};

} // namespace SputterOS

#endif // SPUTTEROS_OSAL_ICOMMANDPRODUCER_H
