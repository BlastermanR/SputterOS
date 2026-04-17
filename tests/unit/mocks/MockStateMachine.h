#ifndef SPUTTEROS_UNIT_MOCKS_MOCKSTATEMACHINE_H
#define SPUTTEROS_UNIT_MOCKS_MOCKSTATEMACHINE_H

#include "sputteros/interfaces/IStateMachine.h"
#include <gmock/gmock.h>

/**
 * @file MockStateMachine.h
 * @brief GoogleMock implementation of IStateMachine<Cfg> for unit testing.
 *
 * Inject a `MockStateMachine<Cfg>` into `ControlTask<Cfg>` to verify that
 * the task calls `init()`, `tick()`, `handleCommand()`, and `forceSafeAbort()`
 * at the correct points in the control loop — without coupling tests to any
 * concrete state machine engine.
 *
 * @tparam Cfg Configuration struct providing `Cfg::Command`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */

namespace SputterOS
{

template <typename Cfg> class MockStateMachine : public IStateMachine<Cfg>
{
  public:
    using CommandStruct = typename Cfg::Command;

    MOCK_METHOD(void, init, (), (override));
    MOCK_METHOD(void, tick, (SputterMicros systemTimeMicros), (override));
    MOCK_METHOD(void, handleCommand, (const CommandStruct &cmd), (override));
    MOCK_METHOD(void, forceSafeAbort, (), (override));
};

} // namespace SputterOS

#endif // SPUTTEROS_UNIT_MOCKS_MOCKSTATEMACHINE_H
