#ifndef SENSORPOLL_CONFIG_SENSORPOLLCONFIG_H
#define SENSORPOLL_CONFIG_SENSORPOLLCONFIG_H

/**
 * @file SensorPollConfig.h
 * @brief Compile-time configuration for the SensorPoll IO_PENDING example.
 *
 * Satisfies the `Cfg` template contract.  Configures a single-core
 * system demonstrating non-blocking IO coordination via the
 * `IO_PENDING` task state.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include <cstddef>
#include <cstdint>

namespace SensorPoll
{

/**
 * @brief Configuration struct for the SensorPoll example.
 */
struct SensorPollConfig
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
    static constexpr std::size_t kCoreCount          = 1;
    static constexpr std::size_t kQueueCapacity      = 16;
};

} // namespace SensorPoll

#endif // SENSORPOLL_CONFIG_SENSORPOLLCONFIG_H
