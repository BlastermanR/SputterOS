#ifndef PINGPONG_INCLUDE_PONGTASK_H
#define PINGPONG_INCLUDE_PONGTASK_H

/**
 * @file PongTask.h
 * @brief Core 1 user task that drives the pong half of the inter-core counter exchange.
 *
 * `PongTask` runs on Core 1 alongside the kernel's `CommsTask` and
 * `DiagnosticsTask`.  When the turn token is released by `PingTask`
 * (`g_pingTurn == false`), `PongTask`:
 *
 *  1. Atomically increments `g_counter` (relaxed: ordering provided by the
 *     release fence on `g_pingTurn` below).
 *  2. Logs "Pong #N -> counter = X" to its `TelemetryLogger` under the
 *     `COMMS` task tag so the label reflects Core 1.
 *  3. Returns the token to Core 0 by storing `g_pingTurn = true` with
 *     `memory_order_release`.
 *
 * `PongTask` imposes no additional rate limit beyond checking the turn flag;
 * the effective exchange rate is governed entirely by `PingTask`'s
 * `kPingIntervalUs` timer.
 *
 * ### Memory Ordering
 * `g_counter` fetch_add uses `memory_order_relaxed`; the subsequent
 * `g_pingTurn.store(true, release)` acts as the visibility fence for the
 * updated counter value seen by `PingTask`'s acquire load on the next tick.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "SharedCounter.h"

#include "sputteros/osal/SputterTime.h"
#include "sputteros/osal/tasks/ITask.h"
#include "sputteros/utils/logging/LightweightStringBuilder.h"
#include "sputteros/utils/logging/TelemetryLogger.h"

#include <atomic>
#include <cstdint>

namespace PingPong
{

/**
 * @brief Core 1 half of the ping-pong counter exchange.
 *
 * Fires immediately whenever `g_pingTurn == false`, increments the
 * shared counter, logs the result, then returns the token to PingTask.
 */
class PongTask : public SputterOS::ITask
{
  public:
    /**
     * @brief Construct a PongTask that writes to the given TelemetryLogger.
     * @param telemetry: Core 1 logger — lifetime must exceed this task.
     */
    explicit PongTask(SputterOS::TelemetryLogger &telemetry) : m_telemetry(telemetry), m_pongCount(0) {}

    /**
     * @brief Reset the pong counter.
     */
    void init() override { m_pongCount = 0; }

    /**
     * @brief Increment the shared counter and return the token to PingTask
     *        when the turn token is held by Core 1.
     *
     * @param systemTimeMicros: Monotonic system time supplied by the scheduler.
     */
    void tick(SputterOS::SputterMicros systemTimeMicros) override
    {
        // Only act when PingTask has released the token to this core.
        if (g_pingTurn.load(std::memory_order_acquire))
        {
            return;
        }

        // Increment the shared counter (relaxed — g_pingTurn release below
        // acts as the visibility fence for this write).
        const uint32_t count = g_counter.fetch_add(1u, std::memory_order_relaxed) + 1u;
        ++m_pongCount;

        // Build and emit telemetry.
        m_builder.clear();
        m_builder.append("[Core 1] Pong #")
            .append(static_cast<uint32_t>(m_pongCount))
            .append(" -> counter = ")
            .append(count);

        m_telemetry.log(SputterOS::TelemetryLogger::TaskID::COMMS, m_builder.c_str(),
                        SputterOS::TelemetryLogger::Verbosity::STATUS, systemTimeMicros);

        // Return the token to PingTask on Core 0.
        g_pingTurn.store(true, std::memory_order_release);
    }

  private:
    SputterOS::TelemetryLogger         &m_telemetry; /**< @brief Core 1 telemetry buffer. */
    SputterOS::LightweightStringBuilder m_builder;   /**< @brief Reusable message formatter. */
    uint32_t                            m_pongCount; /**< @brief Number of pongs emitted so far. */
};

} // namespace PingPong

#endif // PINGPONG_INCLUDE_PONGTASK_H
