#ifndef SPUTTEROS_UNIT_MOCKS_MOCKSAFETYMONITOR_H
#define SPUTTEROS_UNIT_MOCKS_MOCKSAFETYMONITOR_H

#include "sputteros/kernel/interfaces/ISafetyMonitor.h"
#include <gmock/gmock.h>

/**
 * @file MockSafetyMonitor.h
 * @brief GoogleMock implementation of ISafetyMonitor for unit testing.
 *
 * Inject into `ControlTask` test fixtures to simulate safety monitor
 * behaviour — verify that `forceSafeAbort()` is called on the user
 * application when a monitor reports unsafe.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */

namespace SputterOS
{

class MockSafetyMonitor : public ISafetyMonitor
{
  public:
    MOCK_METHOD(bool, isSafe, (), (const, override));
    MOCK_METHOD(const char *, name, (), (const, override));
};

} // namespace SputterOS

#endif // SPUTTEROS_UNIT_MOCKS_MOCKSAFETYMONITOR_H
