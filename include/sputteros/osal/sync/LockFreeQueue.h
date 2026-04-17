#ifndef SPUTTEROS_OSAL_LOCKFREEQUEUE_H
#define SPUTTEROS_OSAL_LOCKFREEQUEUE_H

/**
 * @file LockFreeQueue.h
 * @brief Lock-free, SPSC ring buffer implementing `IMessageQueue<Cfg>`.
 *
 * Provides the mandatory cross-core command queue for SputterOS.
 * The implementation is a single-producer, single-consumer (SPSC) ring
 * buffer backed by `std::atomic` indices with acquire/release semantics.
 *
 * ### Design Constraints
 * - **No mutexes.** All operations are wait-free for both push and pop.
 * - **Bounded capacity.** Fixed at compile time via `Capacity` template
 *   parameter.
 * - **Reject-on-full.** When the buffer is full, `push()` / `try_push()`
 *   return false immediately. The caller (CommsTask) is responsible for
 *   sending a NACK to the host.
 * - **Portable.** Uses only `<atomic>` and `<cstddef>` — no platform
 *   intrinsics.
 *
 * ### Thread-Safety Contract
 * - Exactly **one** thread/core may call `push()` / `try_push()`.
 * - Exactly **one** thread/core may call `pop()` / `try_pop()`.
 * - `size()`, `capacity()`, and `clear()` are safe from either side but
 *   `clear()` should only be called when the queue is quiescent (no
 *   concurrent push/pop).
 *
 * @tparam Cfg   Configuration struct supplying `Cfg::Command`.
 * @tparam Capacity  Maximum number of elements. Must be > 0.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */

#include "sputteros/osal/sync/IMessageQueue.h"

#include <atomic>
#include <cstddef>

namespace SputterOS
{

// Forward declaration — LockFreeQueue is only constructible by System.
template <typename Cfg> class SystemBuilder;

template <typename Cfg, std::size_t Capacity> class LockFreeQueue : public IMessageQueue<Cfg>
{
    static_assert(Capacity > 0, "LockFreeQueue capacity must be greater than 0.");

  public:
    using CommandStruct = typename Cfg::Command;

    // -----------------------------------------------------------------------
    // Lifecycle — construction is gated through System
    // -----------------------------------------------------------------------

  private:
    /**
     * @brief Construct an empty LockFreeQueue.
     *
     * Private — only `System<Cfg>` (as a static member) and
     * `SystemBuilder<Cfg>` may construct a queue instance.
     * Obtain the queue via `System<Cfg>::commandQueue()` after declaring
     * your system topology.
     */
    LockFreeQueue() : m_head(0), m_tail(0)
    {
        // Intentionally Empty — m_buf elements are default-constructed.
    }

    /** @brief Only SystemBuilder / System may create LockFreeQueue instances. */
    friend class SystemBuilder<Cfg>;
    template <typename> friend class System;

  public:
    /**
     * @brief Push a command. Returns immediately (wait-free).
     *
     * The `timeout` parameter is accepted for interface conformance but is
     * ignored — the SPSC ring buffer never blocks.
     *
     * @param cmd     Command to enqueue.
     * @param timeoutMs Ignored (present for IMessageQueue conformance).
     * @return true if the command was enqueued, false if the queue is full.
     */
    bool push(const CommandStruct &cmd, SputterMillis /*timeoutMs*/) override { return try_push(cmd); }

    /**
     * @brief Non-blocking push.
     * @param cmd Command to enqueue.
     * @return true if enqueued, false if full.
     */
    bool try_push(const CommandStruct &cmd) override
    {
        const std::size_t head = m_head.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) % kSlots;

        // If next == tail, the buffer is full.
        if (next == m_tail.load(std::memory_order_acquire))
        {
            return false;
        }

        m_buf[head] = cmd;
        m_head.store(next, std::memory_order_release);
        return true;
    }

    /**
     * @brief Pop a command. Returns immediately (wait-free).
     *
     * The `timeout` parameter is accepted for interface conformance but is
     * ignored — the SPSC ring buffer never blocks.
     *
     * @param cmd       Reference populated with the dequeued command.
     * @param timeoutMs Ignored (present for IMessageQueue conformance).
     * @return true if a command was dequeued, false if the queue is empty.
     */
    bool pop(CommandStruct &cmd, SputterMillis /*timeoutMs*/) override { return try_pop(cmd); }

    /**
     * @brief Non-blocking pop.
     * @param cmd Reference populated with the dequeued command.
     * @return true if dequeued, false if empty.
     */
    bool try_pop(CommandStruct &cmd) override
    {
        const std::size_t tail = m_tail.load(std::memory_order_relaxed);

        // If head == tail, the buffer is empty.
        if (tail == m_head.load(std::memory_order_acquire))
        {
            return false;
        }

        cmd = m_buf[tail];
        m_tail.store((tail + 1) % kSlots, std::memory_order_release);
        return true;
    }

    /**
     * @brief Return the approximate number of items in the queue.
     * @return Number of items (may be slightly stale due to concurrent access).
     */
    std::size_t size() const override
    {
        const std::size_t head = m_head.load(std::memory_order_acquire);
        const std::size_t tail = m_tail.load(std::memory_order_acquire);
        return (head >= tail) ? (head - tail) : (kSlots - tail + head);
    }

    /**
     * @brief Return the usable capacity.
     * @return Capacity (one slot is reserved internally for full/empty disambiguation).
     */
    std::size_t capacity() const override { return Capacity; }

    /**
     * @brief Discard all queued items.
     *
     * @note Only call when no concurrent push/pop is in progress.
     */
    void clear() override { m_tail.store(m_head.load(std::memory_order_relaxed), std::memory_order_release); }

  private:
    /**
     * --------------------
     * Ring Buffer State
     * --------------------
     */

    /** @brief Internal slot count (Capacity + 1 to distinguish full from empty). */
    static constexpr std::size_t kSlots = Capacity + 1;

    CommandStruct            m_buf[kSlots]; /**< @brief Circular storage array. */
    std::atomic<std::size_t> m_head;        /**< @brief Write index (producer). */
    std::atomic<std::size_t> m_tail;        /**< @brief Read index (consumer). */
};

} // namespace SputterOS

#endif // SPUTTEROS_OSAL_LOCKFREEQUEUE_H
