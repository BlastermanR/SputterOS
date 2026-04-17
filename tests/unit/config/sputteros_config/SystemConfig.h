#ifndef SPUTTEROS_CONFIG_SYSTEMCONFIG_H
#define SPUTTEROS_CONFIG_SYSTEMCONFIG_H

#include <cstdint>

namespace SputterOS
{

/**
 * @file SystemConfig.h
 * @brief System-wide enums and command packet used across SputterOS.
 *
 * This header defines the primary system state machine enum, command IDs
 * used for inter-task communication, and the `CommandStruct` packet passed
 * through the OSAL `IMessageQueue` implementations.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/5/2026
 */

/**
 * @brief Master states of the SputterOS state machine.
 */
enum class SystemState
{
    IDLE,
    PUMPING,
    VENTING,
    FAULT_SAFE_MODE
};

/**
 * @brief Command identifiers for inter-task communication.
 *
 * These IDs indicate the semantic meaning of the `CommandStruct` packet.
 */
enum class CommandID : uint8_t
{
    SET_STATE,         /**< Change the global `SystemState` */
    SET_GAS_FLOW,      /**< Set an MFC gas flow setpoint (sccm) */
    SET_POWER_WATTAGE, /**< Set power in watts for a power supply */
    ABORT_PROCESS      /**< Immediately abort the current process */
};

/**
 * @brief Maximum number of commands drained from the queue per `ControlTask` tick.
 *
 * Limits execution time per cycle so a burst of commands cannot starve
 * the rest of the control loop. Tune higher for more command throughput
 * or lower for tighter worst-case tick latency.
 */
static constexpr int kMaxCommandsPerTick = 8;

/**
 * @brief Standard command packet carried by `IMessageQueue`.
 *
 * @note Size should remain small and fixed-size to simplify IPC
 *       implementations (e.g., hardware FIFOs).
 */
struct CommandStruct
{
    CommandID id;           /**< Command identifier */
    uint8_t   targetDevice; /**< Target device index (e.g., MFC channel) */
    float     value;        /**< Command value (units depend on `id`) */
};

} // namespace SputterOS

#endif // SPUTTEROS_CONFIG_SYSTEMCONFIG_H
