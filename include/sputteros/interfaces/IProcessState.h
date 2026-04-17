#ifndef SPUTTEROS_INTERFACES_IPROCESSSTATE_H
#define SPUTTEROS_INTERFACES_IPROCESSSTATE_H

#include "sputteros/osal/SputterTime.h"
#include <cstdint>

/**
 * @file IProcessState.h
 * @brief Interface for a single phase in a process state machine.
 *
 * Implement one concrete `IProcessState` per process phase (e.g. PumpDown,
 * GasStabilization, Deposition). This is a user-space utility — the kernel
 * does not reference this interface directly.
 *
 * `IProcessState` is an **optional companion** to `IUserApplication`. A
 * typical integration stores a pointer to the current state and delegates
 * from `IUserApplication::tick()`:
 *
 * @code
 *   void MyApp::tick(SputterMicros now) { m_currentState->execute(now); }
 * @endcode
 *
 * Applications that do not need a multi-phase state machine can ignore
 * this interface entirely and implement all logic in `IUserApplication`
 * directly.
 *
 * Lifecycle:
 *   1. `onEnter()` — called once when transitioning into this state.
 *   2. `execute()` — called every control tick while this state is active.
 *   3. `onExit()`  — called once when leaving this state.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */

namespace SputterOS
{

class IProcessState
{
  public:
    virtual ~IProcessState() = default;

    virtual void onEnter()                               = 0;
    virtual void execute(SputterMicros systemTimeMicros) = 0;
    virtual void onExit()                                = 0;

    IProcessState(const IProcessState &)            = delete;
    IProcessState &operator=(const IProcessState &) = delete;

  protected:
    IProcessState() = default;
};

} // namespace SputterOS

#endif // SPUTTEROS_INTERFACES_IPROCESSSTATE_H
