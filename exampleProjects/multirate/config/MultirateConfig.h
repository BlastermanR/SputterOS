#ifndef MULTIRATE_CONFIG_MULTIRATECONFIG_H
#define MULTIRATE_CONFIG_MULTIRATECONFIG_H

/**
 * @file MultirateConfig.h
 * @brief Compile-time configuration for the MultiRate scheduling example.
 *
 * Satisfies the `Cfg` template contract required by all SputterOS
 * template classes.  Configures a single-core system with three
 * scheduled tasks at different frequencies, demonstrating how the
 * Cruncher dispatches multi-rate workloads.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include <cstddef>
#include <cstdint>

namespace Multirate
{

/**
 * @brief Configuration struct for the MultiRate scheduling example.
 */
struct MultirateConfig
{
    /**
     * @brief Master states — permanently IDLE for this demo.
     */
    enum class State : uint8_t
    {
        IDLE = 0
    };

    /**
     * @brief Command identifiers — no real commands needed.
     */
    enum class CmdID : uint8_t
    {
        NOP = 0
    };

    /**
     * @brief Standard command packet carried by `IMessageQueue`.
     */
    struct Command
    {
        CmdID   id;           /**< @brief Command identifier. */
        uint8_t targetDevice; /**< @brief Target device index (unused). */
        float   value;        /**< @brief Command value (unused). */
    };

    /** @brief Maximum commands drained per ControlTask tick. */
    static constexpr int kMaxCommandsPerTick = 4;

    /** @brief Highest valid raw command ID accepted by the parser. */
    static constexpr uint8_t kMaxValidCommandID = 0;

    /** @brief Single-core system — all tasks on core 0. */
    static constexpr std::size_t kCoreCount = 1;

    /** @brief Command queue capacity (slots). */
    static constexpr std::size_t kQueueCapacity = 16;
};

} // namespace Multirate

#endif // MULTIRATE_CONFIG_MULTIRATECONFIG_H
