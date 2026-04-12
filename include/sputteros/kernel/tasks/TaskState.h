#ifndef SPUTTEROS_KERNEL_TASKS_TASKSTATE_H
#define SPUTTEROS_KERNEL_TASKS_TASKSTATE_H

/**
 * @file TaskState.h
 * @brief Per-task lifecycle states for scheduled tasks.
 *
 * Each task managed by the Cruncher carries a TaskState that gates
 * dispatch decisions and surfaces diagnostics. Transitions are
 * managed by the scheduling infrastructure, not by the task itself.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include <cstdint>

namespace SputterOS
{
namespace Kernel
{

/**
 * @brief Lifecycle state of a single scheduled task.
 */
enum class TaskState : uint8_t
{
    UNINITIALIZED, /**< @brief Registered but init() not yet called. */
    IDLE,          /**< @brief Initialized, waiting for next activation. */
    READY,         /**< @brief Activation time reached, waiting for dispatch. */
    RUNNING,       /**< @brief Currently executing tick(). */
    IO_PENDING,    /**< @brief Task yielded early — waiting for IO completion. */
    OVERRUN,       /**< @brief Last tick exceeded period — still scheduled but flagged. */
    SUSPENDED,     /**< @brief Temporarily disabled (user or kernel request). */
    FAULTED        /**< @brief Unrecoverable error — will not be scheduled. */
};

} // namespace Kernel
} // namespace SputterOS

#endif // SPUTTEROS_KERNEL_TASKS_TASKSTATE_H
