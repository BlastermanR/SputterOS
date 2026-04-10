/**
 * @file test_StateMachinePipeline.cpp
 * @brief System tests for IProcessState integration through IUserApplication.
 *
 * Defines concrete IProcessState implementations (IdleState, RunningState,
 * FaultState) inline and wires them into a test IUserApplication. Verifies:
 * - System::init() drives IUserApplication::init() which enters the initial
 *   state (onEnter() called once).
 * - Each System::tick() drives execute() on the current state.
 * - A command dispatched by ControlTask causes the application to transition
 *   (onExit() / onEnter() called in order).
 * - A safety abort (TrippableMonitor + forceSafeAbort()) transitions to the
 *   Fault state and the transition lifecycle is correct.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "SystemTestConfig.h"
#include "SystemTestHelpers.h"

#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/interfaces/IProcessState.h"
#include "sputteros/kernel/System.h"
#include "sputteros/utils/logging/ErrorLogger.h"

#include "../../unit/mocks/KernelTestAccess.h"

#include <gtest/gtest.h>

using namespace SputterOS;
using namespace SputterOS::SystemTests;

using Cfg = SingleCoreConfig;

// =========================================================================
// Concrete IProcessState implementations
// =========================================================================

struct StateCounters
{
    uint32_t enterCount   = 0;
    uint32_t executeCount = 0;
    uint32_t exitCount    = 0;
};

class IdleState : public IProcessState
{
  public:
    void onEnter() override { ++counters.enterCount; }
    void execute(SputterMicros) override { ++counters.executeCount; }
    void onExit() override { ++counters.exitCount; }

    StateCounters counters;
};

class RunningState : public IProcessState
{
  public:
    void onEnter() override { ++counters.enterCount; }
    void execute(SputterMicros) override { ++counters.executeCount; }
    void onExit() override { ++counters.exitCount; }

    StateCounters counters;
};

class FaultState : public IProcessState
{
  public:
    void onEnter() override { ++counters.enterCount; }
    void execute(SputterMicros) override { ++counters.executeCount; }
    void onExit() override { ++counters.exitCount; }

    StateCounters counters;
};

// =========================================================================
// Test IUserApplication using the state machine pattern
// =========================================================================

class StateMachineApp : public IUserApplication<Cfg>
{
  public:
    StateMachineApp(IdleState *idle, RunningState *running, FaultState *fault)
        : m_idle(idle), m_running(running), m_fault(fault), m_current(nullptr)
    {
    }

    void init() override { transitionTo(m_idle); }

    void tick(SputterMicros now) override
    {
        if (m_current)
            m_current->execute(now);
    }

    void handleCommand(const Cfg::Command &cmd) override
    {
        // SET_STATE with a non-zero value transitions to Running.
        if (cmd.id == Cfg::CmdID::SET_STATE && cmd.value > 0.5f)
        {
            transitionTo(m_running);
        }
    }

    void forceSafeAbort() override
    {
        ++abortCount;
        transitionTo(m_fault);
        System<Cfg>::errorLogger().log(ErrorLogger::ErrorCode::SOFT_ABORT, SputterMicros(0), 0.0f);
    }

    uint32_t abortCount = 0;

  private:
    void transitionTo(IProcessState *next)
    {
        if (m_current && m_current != next)
            m_current->onExit();
        m_current = next;
        if (m_current)
            m_current->onEnter();
    }

    IdleState    *m_idle;
    RunningState *m_running;
    FaultState   *m_fault;
    IProcessState *m_current;
};

// =========================================================================
// Fixture
// =========================================================================

class StateMachinePipeline : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<Cfg>(); }
};

// =========================================================================
// Tests
// =========================================================================

TEST_F(StateMachinePipeline, InitEntersIdleState)
{
    IdleState    idle;
    RunningState running;
    FaultState   fault;
    StateMachineApp app(&idle, &running, &fault);

    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<Cfg>::init(0);

    EXPECT_EQ(idle.counters.enterCount, 1u);
    EXPECT_EQ(running.counters.enterCount, 0u);
    EXPECT_EQ(fault.counters.enterCount, 0u);
}

TEST_F(StateMachinePipeline, TickDrivesCurrentStateExecute)
{
    IdleState    idle;
    RunningState running;
    FaultState   fault;
    StateMachineApp app(&idle, &running, &fault);

    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<Cfg>::init(0);
    System<Cfg>::tick(0, SputterMicros(1000));
    System<Cfg>::tick(0, SputterMicros(2000));
    System<Cfg>::tick(0, SputterMicros(3000));

    EXPECT_EQ(idle.counters.executeCount, 3u);
    EXPECT_EQ(running.counters.executeCount, 0u);
}

TEST_F(StateMachinePipeline, CommandTransitionsToRunningState)
{
    IdleState    idle;
    RunningState running;
    FaultState   fault;
    StateMachineApp app(&idle, &running, &fault);

    FakeStreamReader        stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<Cfg>::init(0);

    // Push a SET_STATE command with value=1.0 to trigger the IDLE→RUNNING transition.
    Cfg::Command cmd{Cfg::CmdID::SET_STATE, 0, 1.0f};
    System<Cfg>::commandQueue().try_push(cmd);

    // Two ticks: ControlTask pops the command and calls handleCommand().
    System<Cfg>::tick(0, SputterMicros(1000));
    System<Cfg>::tick(0, SputterMicros(2000));

    EXPECT_EQ(idle.counters.exitCount, 1u)    << "Idle::onExit() must be called on transition";
    EXPECT_EQ(running.counters.enterCount, 1u) << "Running::onEnter() must be called on transition";

    // One more tick should execute RunningState, not IdleState.
    System<Cfg>::tick(0, SputterMicros(3000));
    EXPECT_GE(running.counters.executeCount, 1u);
}

TEST_F(StateMachinePipeline, SafetyAbortTransitionsToFaultState)
{
    IdleState    idle;
    RunningState running;
    FaultState   fault;
    StateMachineApp app(&idle, &running, &fault);

    FakeStreamReader stream;
    TrippableMonitor monitor;
    monitor.safeFlag        = false; // Immediately unsafe → abort on first tick
    ISafetyMonitor *monitors[] = {&monitor};

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<Cfg>::init(0);
    System<Cfg>::tick(0, SputterMicros(1000));

    EXPECT_GE(app.abortCount, 1u)              << "forceSafeAbort() must be called";
    EXPECT_EQ(fault.counters.enterCount, 1u)   << "FaultState::onEnter() must be called";

    // A SOFT_ABORT entry must have been logged by the app.
    bool               found = false;
    ErrorLogger::Entry entry{};
    while (System<Cfg>::errorLogger().read(entry))
    {
        if (entry.code == ErrorLogger::ErrorCode::SOFT_ABORT)
        {
            found = true;
        }
    }
    EXPECT_TRUE(found) << "SOFT_ABORT must be logged on safety abort";
}
