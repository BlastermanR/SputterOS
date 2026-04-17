#ifndef PINGPONG_INCLUDE_SHAREDCOUNTER_H
#define PINGPONG_INCLUDE_SHAREDCOUNTER_H

/**
 * @file SharedCounter.h
 * @brief Atomic shared state for cross-core counter passing in the PingPong example.
 *
 * Declares three `inline std::atomic` variables that coordinate the
 * ping-pong exchange between `PingTask` (Core 0) and `PongTask` (Core 1):
 *
 *  - `g_counter`   — monotonically incrementing counter, advanced by one
 *                    on each turn (one increment per ping, one per pong).
 *  - `g_pingTurn`  — token flag: `true` = PingTask's turn,
 *                    `false` = PongTask's turn.
 *  - `g_running`   — system shutdown flag; cleared by PingTask once
 *                    `kMaxRounds` ping-pong cycles are complete.
 *
 * ### Memory Ordering
 * Writers use `memory_order_release`; readers use `memory_order_acquire`.
 * This guarantees that counter increments written before a turn-flag
 * release are visible to the peer core performing the acquire load.
 *
 * @note All variables are `inline` (C++17) so they have exactly one
 *       definition across all translation units that include this header.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include <atomic>
#include <cstdint>

namespace PingPong
{

/**
 * @brief Shared monotonic counter incremented alternately by PingTask and PongTask.
 *
 * Each task performs an `atomic::fetch_add(1, relaxed)` under the
 * protection of the `g_pingTurn` token; the relaxed ordering is safe
 * because the release/acquire on `g_pingTurn` establishes the
 * happens-before relationship.
 */
inline std::atomic<uint32_t> g_counter{0};

/**
 * @brief Turn token: `true` = PingTask (Core 0) acts next,
 *        `false` = PongTask (Core 1) acts next.
 *
 * Written with `memory_order_release`, read with `memory_order_acquire`.
 * The release fence ensures the counter increment is visible to the
 * core that performs the acquire load on the next poll.
 */
inline std::atomic<bool> g_pingTurn{true};

/**
 * @brief Shutdown flag: `false` once PingTask has completed `kMaxRounds` pings.
 *
 * Both tick loops check this with `memory_order_acquire` each iteration
 * and exit gracefully when it becomes `false`.
 */
inline std::atomic<bool> g_running{true};

/**
 * @brief Number of complete ping rounds before the example terminates.
 *
 * Each round consists of one PingTask increment followed by one PongTask
 * increment.  With `kMaxRounds = 5` the counter reaches 9 and the
 * terminal shows 5 "Ping" lines interleaved with 4 "Pong" lines.
 *
 * (PingTask sets `g_running = false` after its 5th ping without signalling
 * PongTask, so the final "Pong #5" is intentionally omitted to produce a
 * clean termination.)
 */
static constexpr uint32_t kMaxRounds = 5;

} // namespace PingPong

#endif // PINGPONG_INCLUDE_SHAREDCOUNTER_H
