#ifndef LIFECYCLE_INCLUDE_LIFECYCLESTATEMACHINE_H
#define LIFECYCLE_INCLUDE_LIFECYCLESTATEMACHINE_H

/**
 * @file LifecycleStateMachine.h
 * @brief Idle-only user application for the Lifecycle example.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "LifecycleConfig.h"
#include "sputteros/kernel/interfaces/IUserApplication.h"

namespace Lifecycle
{

/**
 * @brief Trivial IUserApplication — permanently in IDLE.
 */
class LifecycleApplication : public SputterOS::IUserApplication<LifecycleConfig>
{
  public:
    void init() override {}
    void tick(SputterOS::SputterMicros /*systemTimeMicros*/) override {}
    void handleCommand(const LifecycleConfig::Command & /*cmd*/) override {}
    void forceSafeAbort() override {}
};

} // namespace Lifecycle

#endif // LIFECYCLE_INCLUDE_LIFECYCLESTATEMACHINE_H
