#ifndef SENSORPOLL_INCLUDE_SENSORPOLLSTATEMACHINE_H
#define SENSORPOLL_INCLUDE_SENSORPOLLSTATEMACHINE_H

/**
 * @file SensorPollStateMachine.h
 * @brief Idle-only user application for the SensorPoll example.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "SensorPollConfig.h"
#include "sputteros/kernel/interfaces/IUserApplication.h"

namespace SensorPoll
{

/**
 * @brief Trivial IUserApplication — permanently in IDLE.
 */
class SensorPollApplication : public SputterOS::IUserApplication<SensorPollConfig>
{
  public:
    void init() override {}
    void tick(SputterOS::SputterMicros /*systemTimeMicros*/) override {}
    void handleCommand(const SensorPollConfig::Command & /*cmd*/) override {}
    void forceSafeAbort() override {}
};

} // namespace SensorPoll

#endif // SENSORPOLL_INCLUDE_SENSORPOLLSTATEMACHINE_H
