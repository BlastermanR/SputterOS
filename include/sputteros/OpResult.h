#ifndef SPUTTEROS_OPRESULT_H
#define SPUTTEROS_OPRESULT_H

/**
 * @file OpResult.h
 * @brief Lightweight status code for kernel and utility operations.
 *
 * Replaces bare `bool` returns where the failure reason varies
 * (null argument, capacity full, invalid parameter, etc.). Keeps
 * embedded overhead minimal — a single `uint8_t` with no heap impact.
 *
 * @note `BuildResult` is intentionally kept separate, it carries
 *       structured error context specific to `SystemBuilder::build()`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include <cstdint>

namespace SputterOS
{

/**
 * @brief Status code returned by registration and mutation operations.
 *
 * Usage:
 * @code
 *   OpResult r = im.registerCondition(&cond);
 *   if (r != OpResult::OK) { handleError(r); }
 * @endcode
 */
enum class OpResult : uint8_t
{
    OK       = 0, /**< @brief Operation succeeded. */
    FULL     = 1, /**< @brief Container capacity exhausted. */
    NULL_ARG = 2, /**< @brief A required pointer argument was nullptr. */
};

/**
 * @brief Convenience check: true when the operation succeeded.
 */
inline bool succeeded(OpResult r) { return r == OpResult::OK; }

} // namespace SputterOS

#endif // SPUTTEROS_OPRESULT_H
