#ifndef SPUTTEROS_UNIT_CONFIG_TESTCONFIG_H
#define SPUTTEROS_UNIT_CONFIG_TESTCONFIG_H

/**
 * @file TestConfig.h
 * @brief Compile-time configuration for SputterOS unit tests.
 *
 * Defines the `TestConfig` struct that satisfies the `Cfg` template
 * contract required by all SputterOS template classes (`IMessageQueue<Cfg>`,
 * `IUserApplication<Cfg>`, `CommandParser<Cfg>`, `CLI<Cfg>`, `CommsTask<Cfg>`,
 * `ControlTask<Cfg>`).
 *
 * This replaces the old include-path-hijacked `SystemConfig.h`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */

#include <cstddef>
#include <cstdint>

namespace SputterOS
{

/**
 * @brief Test configuration struct supplying compile-time enums and tuning.
 */
struct TestConfig
{
    /**
     * @brief Master states of the SputterOS state machine.
     */
    enum class State
    {
        IDLE,
        PUMPING,
        VENTING,
        FAULT_SAFE_MODE
    };

    /**
     * @brief Command identifiers for inter-task communication.
     */
    enum class CmdID : uint8_t
    {
        SET_STATE         = 0,
        SET_GAS_FLOW      = 1,
        SET_POWER_WATTAGE = 2,
        ABORT_PROCESS     = 3
    };

    /**
     * @brief Standard command packet carried by `IMessageQueue`.
     */
    struct Command
    {
        CmdID   id;           /**< Command identifier. */
        uint8_t targetDevice; /**< Target device index (e.g. MFC channel). */
        float   value;        /**< Command value (units depend on `id`). */
    };

    /** @brief Maximum commands drained per ControlTask tick. */
    static constexpr int kMaxCommandsPerTick = 8;

    /** @brief Highest valid raw command ID for parser validation. */
    static constexpr uint8_t kMaxValidCommandID = 3;

    /** @brief Number of CPU cores (1 = single-core, 2+ = multi-core). */
    static constexpr std::size_t kCoreCount = 1;

    /** @brief Lock-free command queue capacity. */
    static constexpr std::size_t kQueueCapacity = 16;
};

} // namespace SputterOS

#endif // SPUTTEROS_UNIT_CONFIG_TESTCONFIG_H
