#ifndef LIFECYCLE_CONFIG_LIFECYCLECONFIG_H
#define LIFECYCLE_CONFIG_LIFECYCLECONFIG_H

/**
 * @file LifecycleConfig.h
 * @brief Compile-time configuration for the Lifecycle kernel state demo.
 *
 * Configures a dual-core system to demonstrate kernel state machine
 * transitions: RUNNING → SUSPENDING → SUSPENDED → RUNNING →
 * SHUTTING_DOWN → SHUTDOWN.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include <cstddef>
#include <cstdint>

namespace Lifecycle
{

/**
 * @brief Configuration struct for the Lifecycle example.
 */
struct LifecycleConfig
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
};

} // namespace Lifecycle

#endif // LIFECYCLE_CONFIG_LIFECYCLECONFIG_H
