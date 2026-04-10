#ifndef PINGPONG_INCLUDE_PINGTASK_H
#define PINGPONG_INCLUDE_PINGTASK_H

/**
 * @file PingTask.h
 * @brief Core 0 user task that drives the ping half of the inter-core counter exchange.
 *
 * `PingTask` runs on Core 0 alongside the kernel's `ControlTask`.
 * Every `kPingIntervalUs` microseconds — provided it holds the turn token
 * (`g_pingTurn == true`) — it:
 *
 *  1. Atomically increments `g_counter` (relaxed: ordering provided by the
 *     release fence on `g_pingTurn`).
 *  2. Logs "Ping #N -> counter = X" to its `TelemetryLogger` under the
 *     `CONTROL` task tag so the label reflects Core 0.
 *  3. If `m_pingCount < kMaxRounds`: releases the token to Core 1 by
 *     storing `g_pingTurn = false` with `memory_order_release`.
 *  4. If `m_pingCount >= kMaxRounds`: signals shutdown by storing
 *     `g_running = false` (does NOT release the token — no more Pong
 *     needed after the final ping).
 *
 * ### Memory Ordering
 * The counter fetch_add uses `memory_order_relaxed` because the subsequent
 * `g_pingTurn.store(false, release)` acts as the visibility fence for the
 * updated counter value seen by `PongTask`'s acquire load.
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
 * @brief Core 0 half of the ping-pong counter exchange.
 *
 * Fires every `kPingIntervalUs` when the turn token is held, increments
 * the shared counter, logs the result, then either releases the token
 * to PongTask or terminates the run.
 */
class PingTask : public SputterOS::ITask
{
  public:
    /** @brief Minimum interval between successive ping events (microseconds). */
    static constexpr SputterOS::SputterMicros kPingIntervalUs = 300'000; // 300 ms

    /**
     * @brief Construct a PingTask that writes to the given TelemetryLogger.
     * @param telemetry: Core 0 logger — lifetime must exceed this task.
     */
    explicit PingTask(SputterOS::TelemetryLogger &telemetry)
        : m_telemetry(telemetry), m_lastPing{0}, m_pingCount(0)
    {
    }

    /**
     * @brief Reset the interval timer and ping counter.
     */
    void init() override
    {
        m_lastPing  = 0;
        m_pingCount = 0;
    }

    /**
     * @brief Increment the shared counter and signal PongTask when the
     *        turn token is held and the interval has elapsed.
     *
     * @param systemTimeMicros: Monotonic system time supplied by the scheduler.
     */
    void tick(SputterOS::SputterMicros systemTimeMicros) override
    {
        // Only act when this core holds the turn token.
        if (!g_pingTurn.load(std::memory_order_acquire))
        {
            return;
        }

        // Rate-limit: wait for the ping interval to elapse.
        const SputterOS::SputterMicros elapsed = systemTimeMicros - m_lastPing;
        if (elapsed < kPingIntervalUs)
        {
            return;
        }
        m_lastPing = systemTimeMicros;

        // Increment the shared counter (relaxed — g_pingTurn release below
        // acts as the visibility fence for this write).
        const uint32_t count = g_counter.fetch_add(1u, std::memory_order_relaxed) + 1u;
        ++m_pingCount;

        // Build and emit telemetry.
        m_builder.clear();
        m_builder.append("[Core 0] Ping #")
            .append(static_cast<uint32_t>(m_pingCount))
            .append(" -> counter = ")
            .append(count);

        m_telemetry.log(SputterOS::TelemetryLogger::TaskID::CONTROL, m_builder.c_str(),
                        SputterOS::TelemetryLogger::Verbosity::STATUS, systemTimeMicros);

        if (m_pingCount >= kMaxRounds)
        {
            // All rounds complete — signal both loops to exit.
            // Do NOT release the turn token; no further Pong is needed.
            g_running.store(false, std::memory_order_release);
        }
        else
        {
            // Pass the token to PongTask on Core 1.
            g_pingTurn.store(false, std::memory_order_release);
        }
    }

  private:
    SputterOS::TelemetryLogger         &m_telemetry; /**< @brief Core 0 telemetry buffer. */
    SputterOS::LightweightStringBuilder m_builder;   /**< @brief Reusable message formatter. */
    SputterOS::SputterMicros            m_lastPing;  /**< @brief Timestamp of the last ping event (µs). */
    uint32_t                            m_pingCount; /**< @brief Number of pings emitted so far. */
};

} // namespace PingPong

#endif // PINGPONG_INCLUDE_PINGTASK_H
