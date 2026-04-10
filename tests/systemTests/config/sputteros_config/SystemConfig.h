#ifndef SPUTTEROS_SYSTEMTESTS_SYSTEMCONFIG_H
#define SPUTTEROS_SYSTEMTESTS_SYSTEMCONFIG_H

#include <cstdint>

namespace SputterOS
{

enum class SystemState
{
    IDLE,
    PUMPING,
    VENTING,
    FAULT_SAFE_MODE
};

enum class CommandID : uint8_t
{
    SET_STATE,
    SET_GAS_FLOW,
    SET_POWER_WATTAGE,
    ABORT_PROCESS
};

static constexpr int kMaxCommandsPerTick = 8;

struct CommandStruct
{
    CommandID id;
    uint8_t   targetDevice;
    float     value;
};

} // namespace SputterOS

#endif // SPUTTEROS_SYSTEMTESTS_SYSTEMCONFIG_H
