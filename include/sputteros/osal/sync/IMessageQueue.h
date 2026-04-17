#ifndef SPUTTEROS_OSAL_IMESSAGEQUEUE_H
#define SPUTTEROS_OSAL_IMESSAGEQUEUE_H

#include "sputteros/osal/sync/ICommandConsumer.h"
#include "sputteros/osal/sync/ICommandProducer.h"
#include "sputteros/osal/SputterTime.h"

#include <cstddef>
#include <cstdint>

/**
 * @file IMessageQueue.h
 * @brief Interface for an OS abstraction message queue.
 *
 * Provides bounded push/pop semantics for exchanging command packets
 * between tasks. The element type is injected via the `Cfg` template
 * parameter (`Cfg::Command`), removing the compile-time dependency on
 * a user-supplied `SystemConfig.h`.
 *
 * Inherits `ICommandProducer<Cfg>` and `ICommandConsumer<Cfg>` so that
 * tasks may receive a directional-only view of the queue (producer or
 * consumer) while the full `IMessageQueue` retains both capabilities.
 *
 * Implementations decide back-pressure policy (drop-oldest, reject-new,
 * overwrite, etc.) and MUST document that behavior.
 *
 * @note Callers SHOULD use bounded timeouts when calling blocking operations
 *       to avoid system deadlocks.
 * @note Implementations intended for ISR usage must document safe-from-ISR
 *       functions and semantics.
 *
 * @tparam Cfg Configuration struct providing `Cfg::Command`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */

namespace SputterOS
{

template <typename Cfg> class IMessageQueue : public ICommandProducer<Cfg>, public ICommandConsumer<Cfg>
{
  public:
    using CommandStruct = typename Cfg::Command;

    /**
     * @brief Virtual destructor.
     */
    virtual ~IMessageQueue() = default;

    /**
     * @brief Push a command into the queue, blocking up to timeout.
     * @param cmd Command to enqueue.
     * @param timeoutMs Maximum time to wait in milliseconds. 0 = non-blocking.
     * @return true if the command was accepted, false on timeout or error.
     */
    virtual bool push(const CommandStruct &cmd, SputterMillis timeoutMs) = 0;

    /**
     * @brief Attempt to push a command without blocking.
     * @param cmd Command to enqueue.
     * @return true if the command was accepted, false otherwise.
     *
     * Default implementation calls `push(cmd, 0)` and may be overridden by
     * implementations for greater efficiency.
     */
    virtual bool try_push(const CommandStruct &cmd) { return push(cmd, 0); }

    /**
     * @brief Pop a command from the queue, blocking up to timeout.
     * @param cmd Reference to receive the dequeued command.
     * @param timeoutMs Maximum time to wait in milliseconds. 0 = non-blocking.
     * @return true if a command was returned, false on timeout/empty.
     */
    virtual bool pop(CommandStruct &cmd, SputterMillis timeoutMs) = 0;

    /**
     * @brief Attempt to pop a command without blocking.
     * @param cmd Reference to receive the dequeued command.
     * @return true if a command was returned, false otherwise.
     *
     * Default implementation calls `pop(cmd, 0)` and may be overridden.
     */
    virtual bool try_pop(CommandStruct &cmd) { return pop(cmd, 0); }

    /**
     * @brief Return the approximate number of items in the queue.
     * @return number of items or 0 if unsupported/unknown.
     */
    virtual std::size_t size() const { return 0; }

    /**
     * @brief Return the queue capacity. 0 indicates unbounded or unsupported.
     */
    virtual std::size_t capacity() const { return 0; }

    /**
     * @brief Optional: clear the queue contents. Default is no-op.
     */
    virtual void clear() {}

  protected:
    IMessageQueue() = default;
};

} // namespace SputterOS

#endif // SPUTTEROS_OSAL_IMESSAGEQUEUE_H
