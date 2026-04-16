/**
 * @file test_SafetyAbortBridge.cpp
 * @brief Unit tests for the System<Cfg> safety abort bridge.
 *
 * Validates `signalSafetyAbort()`, `isSafetyAborted()`, and
 * `clearSafetyAbort()` static methods, including reset semantics and
 * integration with `ScheduledControlTask::evaluateSafety()`.
 *
 * Each test uses a unique `AbortTestCfg<N>` to isolate System<Cfg>
 * inline static state.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/13/2026
 */

#include "KernelTestAccess.h"
#include "MockSafetyMonitor.h"
#include "MockStreamReader.h"
#include "MockUserApplication.h"
#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/System.h"

#include <gtest/gtest.h>

using namespace SputterOS;
using namespace SputterOS::Kernel;
using ::testing::NiceMock;
using ::testing::Return;

// ===========================================================================
// Per-test isolated configuration
// ===========================================================================

template <int N> struct AbortTestCfg
{
    enum class State : uint8_t
    {
        IDLE = 0
    };
    enum class CmdID : uint8_t
    {
        NONE = 0
    };
    struct Command
    {
        CmdID   id;
        uint8_t targetDevice;
        float   value;
    };
    static constexpr int         kMaxCommandsPerTick = 8;
    static constexpr uint8_t     kMaxValidCommandID  = 0;
    static constexpr std::size_t kCoreCount          = 1;
    static constexpr std::size_t kQueueCapacity      = 16;
};

// ===========================================================================
// Signal / Check / Clear
// ===========================================================================

TEST(SafetyAbortBridgeTest, DefaultState_NotAborted)
{
    using Cfg = AbortTestCfg<0>;
    KernelTestAccess::resetSystem<Cfg>();

    EXPECT_FALSE(System<Cfg>::isSafetyAborted());
}

TEST(SafetyAbortBridgeTest, SignalSafetyAbort_SetsFlag)
{
    using Cfg = AbortTestCfg<1>;
    KernelTestAccess::resetSystem<Cfg>();

    System<Cfg>::signalSafetyAbort();
    EXPECT_TRUE(System<Cfg>::isSafetyAborted());
}

TEST(SafetyAbortBridgeTest, ClearSafetyAbort_ResetsFlag)
{
    using Cfg = AbortTestCfg<2>;
    KernelTestAccess::resetSystem<Cfg>();

    System<Cfg>::signalSafetyAbort();
    EXPECT_TRUE(System<Cfg>::isSafetyAborted());

    System<Cfg>::clearSafetyAbort();
    EXPECT_FALSE(System<Cfg>::isSafetyAborted());
}

TEST(SafetyAbortBridgeTest, ResetSystem_ClearsSafetyAbort)
{
    using Cfg = AbortTestCfg<3>;
    KernelTestAccess::resetSystem<Cfg>();

    System<Cfg>::signalSafetyAbort();
    EXPECT_TRUE(System<Cfg>::isSafetyAborted());

    KernelTestAccess::resetSystem<Cfg>();
    EXPECT_FALSE(System<Cfg>::isSafetyAborted());
}

// ===========================================================================
// ControlTask integration — safety failure signals abort
// ===========================================================================

TEST(SafetyAbortBridgeTest, ControlTaskSafetyFailure_SignalsAbort)
{
    using Cfg = AbortTestCfg<4>;
    KernelTestAccess::resetSystem<Cfg>();

    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        unsafeMonitor;
    ISafetyMonitor                    *monitors[] = {&unsafeMonitor};

    ON_CALL(unsafeMonitor, isSafe()).WillByDefault(Return(false));

    auto task = KernelTestAccess::makeControlTask<Cfg>(nullptr, &app, monitors, 1);

    // Before tick — no abort
    EXPECT_FALSE(System<Cfg>::isSafetyAborted());

    // Tick triggers evaluateSafety → signalSafetyAbort
    task.tick(SputterMicros(0));

    EXPECT_TRUE(System<Cfg>::isSafetyAborted());
}

TEST(SafetyAbortBridgeTest, ControlTaskSafetyPass_DoesNotSignalAbort)
{
    using Cfg = AbortTestCfg<5>;
    KernelTestAccess::resetSystem<Cfg>();

    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        safeMonitor;
    ISafetyMonitor                    *monitors[] = {&safeMonitor};

    ON_CALL(safeMonitor, isSafe()).WillByDefault(Return(true));

    auto task = KernelTestAccess::makeControlTask<Cfg>(nullptr, &app, monitors, 1);

    task.tick(SputterMicros(0));

    EXPECT_FALSE(System<Cfg>::isSafetyAborted());
}
