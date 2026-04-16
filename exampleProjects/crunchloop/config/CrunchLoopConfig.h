#ifndef CRUNCHLOOP_CONFIG_CRUNCHLOOPCONFIG_H
#define CRUNCHLOOP_CONFIG_CRUNCHLOOPCONFIG_H

/**
 * @file CrunchLoopConfig.h
 * @brief Compile-time configuration for the CrunchLoop example.
 *
 * Configures a dual-core system where Core 0 runs the standard
 * FLAT_LOOP scheduler and Core 1 runs an exclusive ICrunchTask
 * in CRUNCH dispatch mode.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/14/2026
 */

#include <cstddef>
#include <cstdint>

namespace CrunchLoop
{

/**
 * @brief Configuration struct for the CrunchLoop example.
 */
struct CrunchLoopConfig
{
    enum class State : uint8_t
    {
        IDLE = 0
    };

    enum class CmdID : uint8_t
    {
        NOP = 0
    };

    struct Command
    {
        CmdID   id;           /**< @brief Command identifier. */
        uint8_t targetDevice; /**< @brief Target device index (unused). */
        float   value;        /**< @brief Command value (unused). */
    };

    static constexpr int         kMaxCommandsPerTick = 4;
    static constexpr uint8_t     kMaxValidCommandID  = 0;
    static constexpr std::size_t kCoreCount          = 2;
    static constexpr std::size_t kQueueCapacity      = 16;
    static constexpr uint32_t    kCrunchMaxOverruns  = 10;
};

} // namespace CrunchLoop

#endif // CRUNCHLOOP_CONFIG_CRUNCHLOOPCONFIG_H
