#ifndef SPUTTEROS_KERNEL_CORE_DISPATCH_MODE_H
#define SPUTTEROS_KERNEL_CORE_DISPATCH_MODE_H

/**
 * @file CoreDispatchMode.h
 * @brief Per-core dispatch strategy selector for the SputterOS kernel.
 *
 * Each core in the system operates in exactly one dispatch mode,
 * declared at build time via `SystemBuilder`. `FLAT_LOOP` is the
 * default mode used by all existing cores. `CRUNCH` dedicates the
 * entire core to a single `ICrunchTask` running a tight, blocking-
 * tolerant loop managed by `CrunchDispatcher`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/13/2026
 */

#include <cstdint>

namespace SputterOS
{
namespace Kernel
{

/**
 * @brief Dispatch strategy for a single core.
 *
 * Set per-core during `SystemBuilder::build()`. The `System::run()`
 * method branches on this value to select the appropriate runtime
 * loop for each core.
 */
enum class CoreDispatchMode : uint8_t
{
    FLAT_LOOP, ///< Default: iterate all registered tasks per tick.
    CRUNCH     ///< Dedicated tight loop for a single ICrunchTask.
};

} // namespace Kernel
} // namespace SputterOS

#endif // SPUTTEROS_KERNEL_CORE_DISPATCH_MODE_H
