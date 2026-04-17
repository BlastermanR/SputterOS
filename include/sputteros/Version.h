#ifndef SPUTTEROS_VERSION_H
#define SPUTTEROS_VERSION_H

/**
 * @file Version.h
 * @brief Compile-time version constants for SputterOS.
 *
 * Version numbers follow Semantic Versioning (https://semver.org/).
 * Keep these in sync with the `project(SputterOS VERSION ...)` line
 * in the top-level CMakeLists.txt.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/17/2026
 */

#define SPUTTEROS_VERSION_MAJOR 1
#define SPUTTEROS_VERSION_MINOR 0
#define SPUTTEROS_VERSION_PATCH 0

/** @brief Full version as a single integer: (major * 10000 + minor * 100 + patch). */
#define SPUTTEROS_VERSION_INT \
    (SPUTTEROS_VERSION_MAJOR * 10000 + SPUTTEROS_VERSION_MINOR * 100 + SPUTTEROS_VERSION_PATCH)

/** @brief Version string literal, e.g. "0.5.0". */
#define SPUTTEROS_VERSION_STRING "1.0.0"

namespace SputterOS
{

/** @brief Compile-time version constants. */
struct Version
{
    static constexpr int         major  = SPUTTEROS_VERSION_MAJOR;
    static constexpr int         minor  = SPUTTEROS_VERSION_MINOR;
    static constexpr int         patch  = SPUTTEROS_VERSION_PATCH;
    static constexpr int         asInt  = SPUTTEROS_VERSION_INT;
    static constexpr const char *string = SPUTTEROS_VERSION_STRING;
};

} // namespace SputterOS

#endif // SPUTTEROS_VERSION_H
