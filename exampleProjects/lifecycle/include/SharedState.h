#ifndef LIFECYCLE_INCLUDE_SHARED_STATE_H
#define LIFECYCLE_INCLUDE_SHARED_STATE_H

/**
 * @file SharedState.h
 * @brief Shared atomic variables for lifecycle coordination.
 *
 * All inter-core communication uses `std::atomic` with explicit
 * memory ordering — no mutexes or condition variables.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include <atomic>
#include <cstdint>

namespace Lifecycle
{

/**
 * @brief Global shutdown flag — cleared by main() to stop both cores.
 *
 * Written with `memory_order_release`, read with `memory_order_acquire`.
 */
inline std::atomic<bool> g_running{true};

} // namespace Lifecycle

#endif // LIFECYCLE_INCLUDE_SHARED_STATE_H
