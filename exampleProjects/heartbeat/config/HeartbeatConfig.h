#ifndef HEARTBEAT_CONFIG_HEARTBEATCONFIG_H
#define HEARTBEAT_CONFIG_HEARTBEATCONFIG_H

/**
 * @file HeartbeatConfig.h
 * @brief Compile-time configuration for the HeartBeat system test.
 *
 * Satisfies the `Cfg` template contract required by all SputterOS
 * template classes (`ControlTask<Cfg>`, `CommsTask<Cfg>`,
 * `IUserApplication<Cfg>`, `LockFreeQueue<Cfg, N>`, etc.).
 *
 * HeartBeat has no hardware process to control, so a single NOP
 * command identifier is sufficient.  The state machine remains
 * permanently in IDLE; all meaningful output comes from PulseTask.
 *
 * @note To create a Pico variant, provide a parallel HeartbeatConfig_pico.h
 *       with the same struct layout and point the build at that header.
 *       No business logic files need modification.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */

#include <cstddef>
#include <cstdint>

namespace Heartbeat
{

/**
 * @brief Configuration struct for the HeartBeat system test.
 */
struct HeartbeatConfig
{
    /**
     * @brief Master states of the HeartBeat state machine.
     *
     * HeartBeat stays in IDLE throughout its lifecycle.  The enum
     * is present to satisfy the `Cfg::State` convention used by
     * concrete `IUserApplication` implementations.
     */
    enum class State : uint8_t
    {
        IDLE = 0
    };

    /**
     * @brief Command identifiers for inter-task communication.
     *
     * No real commands are needed — NOP is registered so the
     * CommandParser and CLI have a valid sentinel value.
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
        uint8_t targetDevice; /**< @brief Target device index (unused in HeartBeat). */
        float   value;        /**< @brief Command value (unused in HeartBeat). */
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

} // namespace Heartbeat

#endif // HEARTBEAT_CONFIG_HEARTBEATCONFIG_H
