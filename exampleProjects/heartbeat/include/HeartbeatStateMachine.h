#ifndef HEARTBEAT_SRC_HEARTBEATAPPLICATION_H
#define HEARTBEAT_SRC_HEARTBEATAPPLICATION_H

/**
 * @file HeartbeatStateMachine.h
 * @brief Idle-only user application for the HeartBeat system test.
 *
 * The kernel's `ControlTask` requires a concrete `IUserApplication<Cfg>`
 * that implements `init()`, `tick()`, `handleCommand()`, and
 * `forceSafeAbort()`. HeartBeat has no hardware process to control, so
 * every method is a deliberate no-op.
 *
 * All observable output in the HeartBeat example is produced by
 * `PulseTask` — the dedicated fourth task that emits heartbeat telemetry
 * independently of the control loop state.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */

#include "HeartbeatConfig.h"
#include "sputteros/kernel/interfaces/IUserApplication.h"

namespace Heartbeat
{

/**
 * @brief Trivial IUserApplication implementation — permanently in IDLE.
 *
 * Satisfies the `IUserApplication<HeartbeatConfig>` contract required by
 * the kernel's `ControlTask` without driving any real process transitions.
 */
class HeartbeatApplication : public SputterOS::IUserApplication<HeartbeatConfig>
{
  public:
    void init() override {}
    void tick(SputterOS::SputterMicros /*systemTimeMicros*/) override {}
    void handleCommand(const HeartbeatConfig::Command & /*cmd*/) override {}
    void forceSafeAbort() override {}
};

} // namespace Heartbeat

#endif // HEARTBEAT_SRC_HEARTBEATAPPLICATION_H
