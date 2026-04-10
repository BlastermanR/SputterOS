#ifndef SPUTTEROS_UNIT_MOCKS_MOCKFAULTRESPONSE_H
#define SPUTTEROS_UNIT_MOCKS_MOCKFAULTRESPONSE_H

#include "sputteros/logic/IFaultResponse.h"
#include <gmock/gmock.h>

/**
 * @file MockFaultResponse.h
 * @brief GoogleMock implementation of IFaultResponse for unit testing.
 *
 * Inject into `InterlockManager` test fixtures to verify that the fault
 * response is called correctly during `triggerHardFault()`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/6/2026
 */

namespace SputterOS
{

class MockFaultResponse : public IFaultResponse
{
  public:
    MOCK_METHOD(void, execute, (), (override));
};

} // namespace SputterOS

#endif // SPUTTEROS_UNIT_MOCKS_MOCKFAULTRESPONSE_H
