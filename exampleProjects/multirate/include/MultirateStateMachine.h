#ifndef MULTIRATE_INCLUDE_MULTIRATESTATEMACHINE_H
#define MULTIRATE_INCLUDE_MULTIRATESTATEMACHINE_H

/**
 * @file MultirateStateMachine.h
 * @brief Idle-only user application for the MultiRate example.
 *
 * All observable output is produced by the scheduled and background
 * tasks — the user application is a no-op stub to satisfy the
 * `IUserApplication<Cfg>` contract.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "MultirateConfig.h"
#include "sputteros/kernel/interfaces/IUserApplication.h"

namespace Multirate
{

/**
 * @brief Trivial IUserApplication — permanently in IDLE.
 */
class MultirateApplication : public SputterOS::IUserApplication<MultirateConfig>
{
  public:
    void init() override {}
    void tick(SputterOS::SputterMicros /*systemTimeMicros*/) override {}
    void handleCommand(const MultirateConfig::Command & /*cmd*/) override {}
    void forceSafeAbort() override {}
};

} // namespace Multirate

#endif // MULTIRATE_INCLUDE_MULTIRATESTATEMACHINE_H
