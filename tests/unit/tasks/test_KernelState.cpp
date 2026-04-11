/**
 * @file test_KernelState.cpp
 * @brief Unit tests for the KernelState FSM in System<Cfg>.
 *
 * Validates state transitions, invalid transition rejection, error
 * logging on invalid transitions, and full lifecycle paths.
 *
 * Each test uses a unique `KSTestCfg<N>` type to isolate
 * `System<Cfg>` inline static state between tests.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "KernelTestAccess.h"
#include "sputteros/kernel/KernelState.h"
#include "sputteros/kernel/System.h"
#include <gtest/gtest.h>

using namespace SputterOS;
using KS = Kernel::KernelState;

// ===========================================================================
// Per-test isolated configuration
// ===========================================================================

template <int N> struct KSTestCfg
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
// Helper: drive System<Cfg> to a given state via valid transitions
// ===========================================================================

namespace
{

template <typename Cfg> void driveToState(KS target)
{
    // Map: ordered path from UNCONFIGURED to each reachable state
    constexpr KS fullPath[] = {KS::UNCONFIGURED,  KS::CONFIGURED,   KS::INITIALIZING,
                               KS::RUNNING,       KS::SUSPENDING,   KS::SUSPENDED};

    // Walk forward through the standard path
    if (target == KS::UNCONFIGURED)
        return;

    // Standard forward path
    if (target == KS::CONFIGURED || target == KS::INITIALIZING || target == KS::RUNNING)
    {
        const KS steps[] = {KS::CONFIGURED, KS::INITIALIZING, KS::RUNNING};
        for (auto s : steps)
        {
            Kernel::KernelTestAccess::transitionTo<Cfg>(s);
            if (s == target)
                return;
        }
        return;
    }

    // Paths branching from RUNNING
    driveToState<Cfg>(KS::RUNNING);

    if (target == KS::SUSPENDING)
    {
        Kernel::KernelTestAccess::transitionTo<Cfg>(KS::SUSPENDING);
        return;
    }
    if (target == KS::SUSPENDED)
    {
        Kernel::KernelTestAccess::transitionTo<Cfg>(KS::SUSPENDING);
        Kernel::KernelTestAccess::transitionTo<Cfg>(KS::SUSPENDED);
        return;
    }
    if (target == KS::ABORTING)
    {
        Kernel::KernelTestAccess::transitionTo<Cfg>(KS::ABORTING);
        return;
    }
    if (target == KS::ABORTED)
    {
        Kernel::KernelTestAccess::transitionTo<Cfg>(KS::ABORTING);
        Kernel::KernelTestAccess::transitionTo<Cfg>(KS::ABORTED);
        return;
    }
    if (target == KS::SHUTTING_DOWN)
    {
        Kernel::KernelTestAccess::transitionTo<Cfg>(KS::SHUTTING_DOWN);
        return;
    }
    if (target == KS::SHUTDOWN)
    {
        Kernel::KernelTestAccess::transitionTo<Cfg>(KS::SHUTTING_DOWN);
        Kernel::KernelTestAccess::transitionTo<Cfg>(KS::SHUTDOWN);
        return;
    }
}

} // namespace

// ===========================================================================
// Tests
// ===========================================================================

TEST(KernelState, InitialState)
{
    using Cfg = KSTestCfg<0>;
    Kernel::KernelTestAccess::resetKernelState<Cfg>();
    EXPECT_EQ(System<Cfg>::kernelState(), KS::UNCONFIGURED);
}

TEST(KernelState, ValidTransition_ConfiguredToInit)
{
    using Cfg = KSTestCfg<1>;
    Kernel::KernelTestAccess::resetKernelState<Cfg>();

    EXPECT_TRUE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::CONFIGURED));
    EXPECT_EQ(System<Cfg>::kernelState(), KS::CONFIGURED);

    EXPECT_TRUE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::INITIALIZING));
    EXPECT_EQ(System<Cfg>::kernelState(), KS::INITIALIZING);
}

TEST(KernelState, ValidTransition_FullLifecycle)
{
    using Cfg = KSTestCfg<2>;
    Kernel::KernelTestAccess::resetKernelState<Cfg>();

    EXPECT_TRUE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::CONFIGURED));
    EXPECT_TRUE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::INITIALIZING));
    EXPECT_TRUE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::RUNNING));
    EXPECT_TRUE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::SHUTTING_DOWN));
    EXPECT_TRUE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::SHUTDOWN));
    EXPECT_EQ(System<Cfg>::kernelState(), KS::SHUTDOWN);
}

TEST(KernelState, InvalidTransition_UnconfiguredToRunning)
{
    using Cfg = KSTestCfg<3>;
    Kernel::KernelTestAccess::resetKernelState<Cfg>();

    EXPECT_FALSE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::RUNNING));
    EXPECT_EQ(System<Cfg>::kernelState(), KS::UNCONFIGURED);
}

TEST(KernelState, InvalidTransition_ShutdownIsTerminal)
{
    using Cfg = KSTestCfg<4>;
    Kernel::KernelTestAccess::resetKernelState<Cfg>();
    driveToState<Cfg>(KS::SHUTDOWN);
    EXPECT_EQ(System<Cfg>::kernelState(), KS::SHUTDOWN);

    // Every possible target from SHUTDOWN should fail
    EXPECT_FALSE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::UNCONFIGURED));
    EXPECT_FALSE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::CONFIGURED));
    EXPECT_FALSE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::INITIALIZING));
    EXPECT_FALSE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::RUNNING));
    EXPECT_FALSE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::SUSPENDING));
    EXPECT_FALSE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::SUSPENDED));
    EXPECT_FALSE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::ABORTING));
    EXPECT_FALSE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::ABORTED));
    EXPECT_FALSE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::SHUTTING_DOWN));
    EXPECT_FALSE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::SHUTDOWN));
    EXPECT_EQ(System<Cfg>::kernelState(), KS::SHUTDOWN);
}

TEST(KernelState, InvalidTransition_LogsError)
{
    using Cfg = KSTestCfg<5>;
    Kernel::KernelTestAccess::resetSystem<Cfg>();

    // ErrorLogger should be empty after reset
    EXPECT_EQ(System<Cfg>::errorLogger().count(), 0u);

    // Attempt invalid transition: UNCONFIGURED → RUNNING
    EXPECT_FALSE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::RUNNING));

    // Should have logged one INVALID_STATE error
    EXPECT_EQ(System<Cfg>::errorLogger().count(), 1u);

    ErrorLogger::Entry entry{};
    EXPECT_TRUE(System<Cfg>::errorLogger().read(entry));
    EXPECT_EQ(entry.code, ErrorLogger::ErrorCode::INVALID_STATE);
    EXPECT_FLOAT_EQ(entry.value, static_cast<float>(static_cast<uint8_t>(KS::RUNNING)));
}

TEST(KernelState, AbortPath)
{
    using Cfg = KSTestCfg<6>;
    Kernel::KernelTestAccess::resetKernelState<Cfg>();
    driveToState<Cfg>(KS::RUNNING);

    EXPECT_TRUE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::ABORTING));
    EXPECT_EQ(System<Cfg>::kernelState(), KS::ABORTING);

    EXPECT_TRUE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::ABORTED));
    EXPECT_EQ(System<Cfg>::kernelState(), KS::ABORTED);

    // Recovery: ABORTED → RUNNING
    EXPECT_TRUE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::RUNNING));
    EXPECT_EQ(System<Cfg>::kernelState(), KS::RUNNING);
}

TEST(KernelState, SuspendPath)
{
    using Cfg = KSTestCfg<7>;
    Kernel::KernelTestAccess::resetKernelState<Cfg>();
    driveToState<Cfg>(KS::RUNNING);

    EXPECT_TRUE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::SUSPENDING));
    EXPECT_EQ(System<Cfg>::kernelState(), KS::SUSPENDING);

    EXPECT_TRUE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::SUSPENDED));
    EXPECT_EQ(System<Cfg>::kernelState(), KS::SUSPENDED);

    // Resume: SUSPENDED → RUNNING
    EXPECT_TRUE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::RUNNING));
    EXPECT_EQ(System<Cfg>::kernelState(), KS::RUNNING);
}

TEST(KernelState, ShutdownFromAborted)
{
    using Cfg = KSTestCfg<8>;
    Kernel::KernelTestAccess::resetKernelState<Cfg>();
    driveToState<Cfg>(KS::ABORTED);

    EXPECT_TRUE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::SHUTTING_DOWN));
    EXPECT_EQ(System<Cfg>::kernelState(), KS::SHUTTING_DOWN);

    EXPECT_TRUE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::SHUTDOWN));
    EXPECT_EQ(System<Cfg>::kernelState(), KS::SHUTDOWN);
}

TEST(KernelState, ShutdownFromSuspended)
{
    using Cfg = KSTestCfg<9>;
    Kernel::KernelTestAccess::resetKernelState<Cfg>();
    driveToState<Cfg>(KS::SUSPENDED);

    EXPECT_TRUE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::SHUTTING_DOWN));
    EXPECT_EQ(System<Cfg>::kernelState(), KS::SHUTTING_DOWN);

    EXPECT_TRUE(Kernel::KernelTestAccess::transitionTo<Cfg>(KS::SHUTDOWN));
    EXPECT_EQ(System<Cfg>::kernelState(), KS::SHUTDOWN);
}
