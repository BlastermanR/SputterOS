#ifndef SPUTTEROS_UNIT_MOCKS_MOCKPROCESSSTATE_H
#define SPUTTEROS_UNIT_MOCKS_MOCKPROCESSSTATE_H

#include "sputteros/interfaces/IProcessState.h"
#include <gmock/gmock.h>

/**
 * @file MockProcessState.h
 * @brief GoogleMock implementation of IProcessState for unit testing.
 *
 * Register one or more `MockProcessState` instances into a `StateMachine`
 * under test. Use `EXPECT_CALL` to verify that the engine calls `onEnter()`
 * and `onExit()` exactly once per transition and calls `execute()` on
 * every tick while the state is active.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */

namespace SputterOS
{

class MockProcessState : public IProcessState
{
  public:
    MOCK_METHOD(void, onEnter, (), (override));
    MOCK_METHOD(void, execute, (SputterMicros systemTimeMicros), (override));
    MOCK_METHOD(void, onExit, (), (override));
};

} // namespace SputterOS

#endif // SPUTTEROS_UNIT_MOCKS_MOCKPROCESSSTATE_H
