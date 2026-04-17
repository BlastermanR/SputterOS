#ifndef SPUTTEROS_UNIT_MOCKS_MOCKINTERLOCKCONDITION_H
#define SPUTTEROS_UNIT_MOCKS_MOCKINTERLOCKCONDITION_H

#include "sputteros/logic/IInterlockCondition.h"
#include <gmock/gmock.h>

/**
 * @file MockInterlockCondition.h
 * @brief GoogleMock implementation of IInterlockCondition for unit testing.
 *
 * Inject into `InterlockManager` test fixtures to verify that conditions
 * are evaluated correctly during `checkAllInterlocks()`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/6/2026
 */

namespace SputterOS
{

class MockInterlockCondition : public IInterlockCondition
{
  public:
    MOCK_METHOD(bool, isSafe, (), (const, override));
    MOCK_METHOD(const char *, name, (), (const, override));
};

} // namespace SputterOS

#endif // SPUTTEROS_UNIT_MOCKS_MOCKINTERLOCKCONDITION_H
