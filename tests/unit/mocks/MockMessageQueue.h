#ifndef SPUTTEROS_UNIT_MOCKS_MOCKMESSAGEQUEUE_H
#define SPUTTEROS_UNIT_MOCKS_MOCKMESSAGEQUEUE_H

#include "sputteros/osal/sync/IMessageQueue.h"
#include <gmock/gmock.h>

/**
 * @file MockMessageQueue.h
 * @brief GoogleMock implementation of IMessageQueue<Cfg> for unit testing.
 *
 * Inject into `CommsTask` and `ControlTask` test fixtures.
 *
 * For `CommsTask` tests: expect `try_push()` calls when the parser produces
 * a valid command, and verify NACK when `try_push()` returns false.
 *
 * For `ControlTask` tests: use `try_pop()` to feed a controlled sequence of
 * command packets and verify forwarding to the `StateMachine`.
 *
 * @tparam Cfg Configuration struct providing `Cfg::Command`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */

namespace SputterOS
{

template <typename Cfg> class MockMessageQueue : public IMessageQueue<Cfg>
{
  public:
    using CommandStruct = typename Cfg::Command;

    MOCK_METHOD(bool, push, (const CommandStruct &cmd, std::chrono::milliseconds timeout), (override));
    MOCK_METHOD(bool, pop, (CommandStruct & cmd, std::chrono::milliseconds timeout), (override));
    MOCK_METHOD(bool, try_push, (const CommandStruct &cmd), (override));
    MOCK_METHOD(bool, try_pop, (CommandStruct & cmd), (override));
    MOCK_METHOD(std::size_t, size, (), (const, override));
    MOCK_METHOD(std::size_t, capacity, (), (const, override));
    MOCK_METHOD(void, clear, (), (override));
};

} // namespace SputterOS

#endif // SPUTTEROS_UNIT_MOCKS_MOCKMESSAGEQUEUE_H
