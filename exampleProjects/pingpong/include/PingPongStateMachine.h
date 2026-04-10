#ifndef PINGPONG_INCLUDE_PINGPONGSTATEMACHINE_H
#define PINGPONG_INCLUDE_PINGPONGSTATEMACHINE_H

/**
 * @file PingPongStateMachine.h
 * @brief Idle-only user application for the PingPong dual-core example.
 *
 * The kernel's `ControlTask` requires a concrete `IUserApplication<Cfg>`
 * that implements `init()`, `tick()`, `handleCommand()`, and
 * `forceSafeAbort()`. Because the PingPong example has no hardware
 * process to control, every method is a deliberate no-op.
 *
 * All observable output is produced by `PingTask` and `PongTask` — the
 * user tasks that exchange a shared counter between Core 0 and Core 1.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "PingPongConfig.h"
#include "sputteros/kernel/interfaces/IUserApplication.h"

namespace PingPong
{

/**
 * @brief Trivial IUserApplication implementation — permanently in IDLE.
 *
 * Satisfies the `IUserApplication<PingPongConfig>` contract required by
 * the kernel's `ControlTask` without driving any real process transitions.
 */
class PingPongApplication : public SputterOS::IUserApplication<PingPongConfig>
{
  public:
    void init() override {}
    void tick(SputterOS::SputterMicros /*systemTimeMicros*/) override {}
    void handleCommand(const PingPongConfig::Command & /*cmd*/) override {}
    void forceSafeAbort() override {}
};

} // namespace PingPong

#endif // PINGPONG_INCLUDE_PINGPONGSTATEMACHINE_H
