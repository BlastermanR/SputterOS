#ifndef SPUTTEROS_SYSTEMTESTS_SYSTEMTESTCONFIG_H
#define SPUTTEROS_SYSTEMTESTS_SYSTEMTESTCONFIG_H

/**
 * @file SystemTestConfig.h
 * @brief Compile-time configurations for SputterOS system tests.
 *
 * Provides 1-core and 2-core Cfg structs satisfying ConfigValidator.
 */

#include <cstddef>
#include <cstdint>

namespace SputterOS
{
namespace SystemTests
{

/**
 * @brief Single-core configuration for system tests.
 */
struct SingleCoreConfig
{
    enum class State : uint8_t
    {
        IDLE = 0,
        RUNNING,
        FAULT
    };

    enum class CmdID : uint8_t
    {
        SET_STATE     = 0,
        SET_FLOW      = 1,
        ABORT_PROCESS = 2
    };

    struct Command
    {
        CmdID   id;
        uint8_t targetDevice;
        float   value;
    };

    static constexpr int         kMaxCommandsPerTick = 8;
    static constexpr uint8_t     kMaxValidCommandID  = 2;
    static constexpr std::size_t kCoreCount          = 1;
    static constexpr std::size_t kQueueCapacity      = 16;
};

/**
 * @brief Dual-core configuration for multi-core system tests.
 */
struct DualCoreConfig
{
    enum class State : uint8_t
    {
        IDLE = 0,
        RUNNING,
        FAULT
    };

    enum class CmdID : uint8_t
    {
        SET_STATE     = 0,
        SET_FLOW      = 1,
        ABORT_PROCESS = 2
    };

    struct Command
    {
        CmdID   id;
        uint8_t targetDevice;
        float   value;
    };

    static constexpr int         kMaxCommandsPerTick = 8;
    static constexpr uint8_t     kMaxValidCommandID  = 2;
    static constexpr std::size_t kCoreCount          = 2;
    static constexpr std::size_t kQueueCapacity      = 16;
};

} // namespace SystemTests
} // namespace SputterOS

#endif // SPUTTEROS_SYSTEMTESTS_SYSTEMTESTCONFIG_H
