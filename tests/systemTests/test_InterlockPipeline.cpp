/**
 * @file test_InterlockPipeline.cpp
 * @brief System tests for InterlockManager wired as ISafetyMonitor in System<Cfg>.
 *
 * Validates the real-world integration pattern where a concrete
 * InterlockManager is adapted into an ISafetyMonitor and wired into
 * SystemBuilder. Verifies:
 * - All conditions safe → no abort, no fault response.
 * - A tripped condition → ControlTask calls forceSafeAbort().
 * - The hard-fault latch causes IFaultResponse::execute() only once even
 *   across multiple ticks with the condition still violated.
 * - Clearing the condition and calling clearFault() stops further aborts.
 * - Multiple conditions, all safe → no abort.
 *
 * @note InterlockManager does not directly inherit ISafetyMonitor. The
 *       InterlockSafetyAdapter defined here demonstrates the recommended
 *       user-space wiring pattern.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "SystemTestConfig.h"
#include "SystemTestHelpers.h"

#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/System.h"
#include "sputteros/logic/InterlockManager.h"

#include "../../unit/mocks/KernelTestAccess.h"

#include <gtest/gtest.h>

using namespace SputterOS;
using namespace SputterOS::SystemTests;

using Cfg = SingleCoreConfig;

// =========================================================================
// InterlockSafetyAdapter
//
// User-space bridge: wraps InterlockManager as an ISafetyMonitor so it can
// be passed to SystemBuilder. On a failed check the adapter triggers a hard
// fault exactly once (guarded by isHardFaulted()), then keeps returning
// false until the fault is explicitly cleared.
// =========================================================================

class InterlockSafetyAdapter : public ISafetyMonitor
{
  public:
    explicit InterlockSafetyAdapter(InterlockManager *mgr) : m_mgr(mgr) {}

    bool isSafe() const override
    {
        if (!m_mgr->checkAllInterlocks())
        {
            if (!m_mgr->isHardFaulted())
            {
                m_mgr->triggerHardFault(); // execute() called once, then latched
            }
            return false;
        }
        return !m_mgr->isHardFaulted();
    }

  private:
    InterlockManager *m_mgr;
};

// =========================================================================
// Fixture
// =========================================================================

class InterlockPipeline : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<Cfg>(); }
};

// =========================================================================
// Tests
// =========================================================================

TEST_F(InterlockPipeline, AllConditionsSafeNoAbort)
{
    InterlockManager      manager;
    CountingFaultResponse faultResponse;
    TrippableCondition    cond;
    cond.safeFlag = true;
    manager.registerCondition(&cond);
    manager.setFaultResponse(&faultResponse);

    InterlockSafetyAdapter adapter(&manager);
    ISafetyMonitor        *monitors[] = {&adapter};

    InstrumentedApp<Cfg> app;
    FakeStreamReader     stream;

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<Cfg>::init(0);
    System<Cfg>::tick(0, SputterMicros(1000));
    System<Cfg>::tick(0, SputterMicros(2000));

    EXPECT_EQ(app.abortCount, 0u);
    EXPECT_EQ(faultResponse.executeCount, 0u);
}

TEST_F(InterlockPipeline, TrippedConditionTriggersAbort)
{
    InterlockManager      manager;
    CountingFaultResponse faultResponse;
    TrippableCondition    cond;
    cond.safeFlag = false; // Immediately unsafe
    manager.registerCondition(&cond);
    manager.setFaultResponse(&faultResponse);

    InterlockSafetyAdapter adapter(&manager);
    ISafetyMonitor        *monitors[] = {&adapter};

    InstrumentedApp<Cfg> app;
    FakeStreamReader     stream;

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<Cfg>::init(0);
    System<Cfg>::tick(0, SputterMicros(1000));

    EXPECT_GE(app.abortCount, 1u);
}

TEST_F(InterlockPipeline, HardFaultResponseCalledExactlyOnce)
{
    // Even across multiple ticks with the condition still violated,
    // IFaultResponse::execute() must be called exactly once (hard-fault latch).
    InterlockManager      manager;
    CountingFaultResponse faultResponse;
    TrippableCondition    cond;
    cond.safeFlag = false;
    manager.registerCondition(&cond);
    manager.setFaultResponse(&faultResponse);

    InterlockSafetyAdapter adapter(&manager);
    ISafetyMonitor        *monitors[] = {&adapter};

    InstrumentedApp<Cfg> app;
    FakeStreamReader     stream;

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<Cfg>::init(0);
    for (uint64_t t = 1000; t <= 3000; t += 1000)
    {
        System<Cfg>::tick(0, SputterMicros(t));
    }

    EXPECT_EQ(faultResponse.executeCount, 1u);
}

TEST_F(InterlockPipeline, ClearFaultAllowsRecovery)
{
    InterlockManager      manager;
    CountingFaultResponse faultResponse;
    TrippableCondition    cond;
    cond.safeFlag = false;
    manager.registerCondition(&cond);
    manager.setFaultResponse(&faultResponse);

    InterlockSafetyAdapter adapter(&manager);
    ISafetyMonitor        *monitors[] = {&adapter};

    InstrumentedApp<Cfg> app;
    FakeStreamReader     stream;

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<Cfg>::init(0);
    System<Cfg>::tick(0, SputterMicros(1000));
    ASSERT_GE(app.abortCount, 1u);
    ASSERT_EQ(faultResponse.executeCount, 1u);

    // Fix the condition and clear the latch.
    cond.safeFlag = true;
    ASSERT_TRUE(manager.clearFault());

    System<Cfg>::tick(0, SputterMicros(2000));

    // No additional aborts or fault responses after recovery.
    EXPECT_EQ(app.abortCount, 1u);
    EXPECT_EQ(faultResponse.executeCount, 1u);
}

TEST_F(InterlockPipeline, MultipleConditionsAllSafeNoAbort)
{
    InterlockManager      manager;
    CountingFaultResponse faultResponse;
    TrippableCondition    condA;
    TrippableCondition    condB;
    condA.safeFlag = true;
    condB.safeFlag = true;
    manager.registerCondition(&condA);
    manager.registerCondition(&condB);
    manager.setFaultResponse(&faultResponse);

    InterlockSafetyAdapter adapter(&manager);
    ISafetyMonitor        *monitors[] = {&adapter};

    InstrumentedApp<Cfg> app;
    FakeStreamReader     stream;

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<Cfg>::init(0);
    System<Cfg>::tick(0, SputterMicros(1000));
    System<Cfg>::tick(0, SputterMicros(2000));

    EXPECT_EQ(app.abortCount, 0u);
    EXPECT_EQ(faultResponse.executeCount, 0u);
}
