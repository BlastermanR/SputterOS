/**
 * @file test_ControlTask.cpp
 * @brief Unit tests for Kernel::ControlTask<Cfg>.
 *
 * Uses MockUserApplication<Cfg> (IUserApplication<Cfg>) and MockSafetyMonitor
 * (ISafetyMonitor) to verify safety evaluation ordering, command draining,
 * and user application interface forwarding.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */

#include "KernelTestAccess.h"
#include "MockMessageQueue.h"
#include "MockSafetyMonitor.h"
#include "MockUserApplication.h"
#include "TestConfig.h"
#include "sputteros/osal/SputterTime.h"
#include <gtest/gtest.h>

using namespace SputterOS;
using namespace SputterOS::Kernel;
using Cfg           = TestConfig;
using CmdID         = Cfg::CmdID;
using CommandStruct = Cfg::Command;
using ::testing::NiceMock;
using ::testing::Return;

// ===========================================================================
// Fixture
// ===========================================================================

class ControlTaskTest : public ::testing::Test
{
  protected:
    NiceMock<MockMessageQueue<Cfg>>    queue;
    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor1;
    NiceMock<MockSafetyMonitor>        monitor2;
    std::array<ISafetyMonitor *, 2>    monitors = {&monitor1, &monitor2};

    ControlTask<Cfg> task = KernelTestAccess::makeControlTask<Cfg>(&queue, &app, monitors.data(), monitors.size());

    void SetUp() override
    {
        // Default safe conditions
        ON_CALL(monitor1, isSafe()).WillByDefault(Return(true));
        ON_CALL(monitor2, isSafe()).WillByDefault(Return(true));
        // Queue empty by default
        ON_CALL(queue, try_pop(::testing::_)).WillByDefault(Return(false));
    }
};

// ---------------------------------------------------------------------------
// init: forwarded to user application
// ---------------------------------------------------------------------------

TEST_F(ControlTaskTest, Init_ForwardsToUserApplication)
{
    EXPECT_CALL(app, init()).Times(1);
    task.init();
}

// ---------------------------------------------------------------------------
// validateDependencies
// ---------------------------------------------------------------------------

TEST_F(ControlTaskTest, ValidateDependencies_AllNonNull_ReturnsTrue) { EXPECT_TRUE(task.validateDependencies()); }

TEST_F(ControlTaskTest, ValidateDependencies_NullQueue_ReturnsFalse)
{
    auto bad = KernelTestAccess::makeControlTask<Cfg>(nullptr, &app, monitors.data(), monitors.size());
    EXPECT_FALSE(bad.validateDependencies());
}

TEST_F(ControlTaskTest, ValidateDependencies_NullApp_ReturnsFalse)
{
    auto bad = KernelTestAccess::makeControlTask<Cfg>(&queue, nullptr, monitors.data(), monitors.size());
    EXPECT_FALSE(bad.validateDependencies());
}

// ---------------------------------------------------------------------------
// Nominal tick: user application is ticked with the correct timestamp
// ---------------------------------------------------------------------------

TEST_F(ControlTaskTest, NominalTick_AppReceivesTimestamp)
{
    EXPECT_CALL(app, tick(SputterMicros(0))).Times(1);
    EXPECT_CALL(app, tick(SputterMicros(200))).Times(1);
    task.tick(SputterMicros(0));
    task.tick(SputterMicros(200));
}

// ---------------------------------------------------------------------------
// Safety monitor failure: forceSafeAbort, app tick not called
// ---------------------------------------------------------------------------

TEST_F(ControlTaskTest, SafetyMonitorFails_TriggersForceAbort)
{
    ON_CALL(monitor1, isSafe()).WillByDefault(Return(false));
    EXPECT_CALL(app, forceSafeAbort()).Times(::testing::AtLeast(1));
    task.tick(SputterMicros(0));
    task.tick(SputterMicros(100));
}

TEST_F(ControlTaskTest, SafetyMonitorFails_AppTickIsNotCalled)
{
    ON_CALL(monitor1, isSafe()).WillByDefault(Return(false));
    EXPECT_CALL(app, tick(::testing::_)).Times(0);
    task.tick(SputterMicros(0));
    task.tick(SputterMicros(100));
}

TEST_F(ControlTaskTest, SecondMonitorFails_TriggersForceAbort)
{
    ON_CALL(monitor2, isSafe()).WillByDefault(Return(false));
    EXPECT_CALL(app, forceSafeAbort()).Times(::testing::AtLeast(1));
    task.tick(SputterMicros(0));
}

// ---------------------------------------------------------------------------
// Command draining
// ---------------------------------------------------------------------------

TEST_F(ControlTaskTest, CommandDraining_EmptyQueue_SinglePopAttempt)
{
    task.tick(SputterMicros(0)); // baseline tick
    EXPECT_CALL(queue, try_pop(::testing::_)).WillOnce(Return(false));
    task.tick(SputterMicros(100));
}

TEST_F(ControlTaskTest, CommandDraining_SingleCommand_ForwardsToApp)
{
    task.tick(SputterMicros(0)); // baseline with empty queue

    CommandStruct cmd{};
    cmd.id    = CmdID::ABORT_PROCESS;
    cmd.value = 0.0f;

    EXPECT_CALL(queue, try_pop(::testing::_))
        .WillOnce(
            [&](CommandStruct &out)
            {
                out = cmd;
                return true;
            })
        .WillRepeatedly(Return(false));

    EXPECT_CALL(app, handleCommand(::testing::Field(&CommandStruct::id, CmdID::ABORT_PROCESS))).Times(1);

    task.tick(SputterMicros(100));
}

TEST_F(ControlTaskTest, CommandDraining_StopsAtMaxCommandsPerTick)
{
    // Always return a command -- the loop must not drain more than kMaxCommandsPerTick
    CommandStruct cmd{};
    cmd.id = CmdID::SET_GAS_FLOW;
    ON_CALL(queue, try_pop(::testing::_))
        .WillByDefault(
            [&](CommandStruct &out)
            {
                out = cmd;
                return true;
            });

    task.tick(SputterMicros(0)); // baseline tick (ON_CALL active, not counted below)

    EXPECT_CALL(queue, try_pop(::testing::_)).Times(::testing::AtMost(Cfg::kMaxCommandsPerTick));
    task.tick(SputterMicros(100));
}
