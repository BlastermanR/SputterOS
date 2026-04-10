#ifndef SPUTTEROS_UNIT_MOCKS_MOCKUSERAPPLICATION_H
#define SPUTTEROS_UNIT_MOCKS_MOCKUSERAPPLICATION_H

#include "sputteros/kernel/interfaces/IUserApplication.h"
#include <gmock/gmock.h>

/**
 * @file MockUserApplication.h
 * @brief GoogleMock implementation of IUserApplication<Cfg> for unit testing.
 *
 * Inject into `ControlTask` test fixtures to verify that the kernel task
 * correctly calls `init()`, `tick()`, `handleCommand()`, and
 * `forceSafeAbort()` at the correct points in the control loop.
 *
 * @tparam Cfg Configuration struct providing `Cfg::Command`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */

namespace SputterOS
{

template <typename Cfg> class MockUserApplication : public IUserApplication<Cfg>
{
  public:
    using CommandStruct = typename Cfg::Command;

    MOCK_METHOD(void, init, (), (override));
    MOCK_METHOD(void, tick, (SputterMicros systemTimeMicros), (override));
    MOCK_METHOD(void, handleCommand, (const CommandStruct &cmd), (override));
    MOCK_METHOD(void, forceSafeAbort, (), (override));
};

} // namespace SputterOS

#endif // SPUTTEROS_UNIT_MOCKS_MOCKUSERAPPLICATION_H
