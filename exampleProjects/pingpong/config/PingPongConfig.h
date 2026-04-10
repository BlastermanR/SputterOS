#ifndef PINGPONG_CONFIG_PINGPONGCONFIG_H
#define PINGPONG_CONFIG_PINGPONGCONFIG_H

/**
 * @file PingPongConfig.h
 * @brief Compile-time configuration for the PingPong dual-core example.
 *
 * Demonstrates the `kCoreCount = 2` path of SputterOS: the kernel places
 * `ControlTask` on Core 0 and `CommsTask` + `DiagnosticsTask` on Core 1.
 * User tasks (`PingTask`, `PongTask`) are added to the respective cores
 * via `SystemBuilder::core(id).addTask()` and participate in the same
 * round-robin tick loop as the kernel tasks.
 *
 * Because the example has no hardware process to control, a single NOP
 * command and a permanently-IDLE state machine are sufficient.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include <cstddef>
#include <cstdint>

namespace PingPong
{

/**
 * @brief Configuration struct for the PingPong dual-core example.
 *
 * Setting `kCoreCount = 2` activates the full multi-core path in
 * `SystemBuilder` and `System`:
 *  - `ControlTask` (ICriticalTask) is pinned to Core 0.
 *  - `CommsTask` + `DiagnosticsTask` (IAsyncTask) are pinned to Core 1.
 *  - `MultiCoreSync<2>` replaces `NoOpMultiCoreSync` for startup barriers.
 */
struct PingPongConfig
{
    /**
     * @brief Master states of the PingPong state machine.
     *
     * No process is driven, so IDLE is the only valid state.
     */
    enum class State : uint8_t
    {
        IDLE = 0
    };

    /**
     * @brief Command identifiers for inter-task communication.
     *
     * NOP is the only registered command — it satisfies the kernel parser
     * contract without enabling any real command dispatch.
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

    /**
     * @brief Dual-core system.
     *
     * This is the primary differentiator from the Heartbeat example.
     * Setting `kCoreCount >= 2` activates `MultiCoreSync<2>`,
     * dual-core task affinity enforcement, and the startup/shutdown
     * barrier API.
     */
    static constexpr std::size_t kCoreCount = 2;

    /** @brief Command queue capacity (slots). */
    static constexpr std::size_t kQueueCapacity = 16;
};

} // namespace PingPong

#endif // PINGPONG_CONFIG_PINGPONGCONFIG_H
