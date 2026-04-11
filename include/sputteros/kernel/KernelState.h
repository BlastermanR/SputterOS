#ifndef SPUTTEROS_KERNEL_KERNELSTATE_H
#define SPUTTEROS_KERNEL_KERNELSTATE_H

/**
 * @file KernelState.h
 * @brief Top-level lifecycle states for the SputterOS kernel.
 *
 * Enumerates every state the kernel can occupy from initial power-on
 * through normal operation to shutdown or abort. Used by the Cruncher
 * and SystemBuilder to gate transitions and enforce ordering.
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
 * @brief Top-level lifecycle state of the SputterOS kernel.
 */
enum class KernelState : uint8_t
{
    UNCONFIGURED,  /**< @brief Before build(). */
    CONFIGURED,    /**< @brief After build(), before init(). */
    INITIALIZING,  /**< @brief During init() — tasks being initialized. */
    RUNNING,       /**< @brief Normal operation — Cruncher dispatching. */
    SUSPENDING,    /**< @brief Graceful pause requested — drain in-flight tasks. */
    SUSPENDED,     /**< @brief All Crunchers paused — background tasks may still run. */
    ABORTING,      /**< @brief Safety abort in progress. */
    ABORTED,       /**< @brief Safe state reached — waiting for operator. */
    SHUTTING_DOWN, /**< @brief Orderly shutdown — draining tasks. */
    SHUTDOWN       /**< @brief Terminal state. */
};

} // namespace Kernel
} // namespace SputterOS

#endif // SPUTTEROS_KERNEL_KERNELSTATE_H
