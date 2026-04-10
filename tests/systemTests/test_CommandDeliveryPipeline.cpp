/**
 * @file test_CommandDeliveryPipeline.cpp
 * @brief System tests for the IStream → CommsTask → queue → ControlTask pipeline.
 *
 * Verifies end-to-end command delivery using a real System<Cfg> and a
 * ScriptedStream byte buffer:
 * - A valid ASCII command string is parsed and delivered to handleCommand().
 * - Parsed fields (id, targetDevice, value) are correct.
 * - A malformed command string produces no delivery.
 * - A command ID exceeding kMaxValidCommandID is rejected by the parser.
 * - A full queue is handled gracefully (no crash or hang).
 * - An empty stream produces no delivery.
 *
 * @note CommsTask and ControlTask both run on core 0 in single-core mode.
 *       Two ticks are used to allow CommsTask to push and ControlTask to
 *       pop regardless of within-tick ordering.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "SystemTestConfig.h"
#include "SystemTestHelpers.h"

#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/System.h"

#include "../../unit/mocks/KernelTestAccess.h"

#include <gtest/gtest.h>

using namespace SputterOS;
using namespace SputterOS::SystemTests;

using Cfg = SingleCoreConfig;
using Cmd = Cfg::Command;

// =========================================================================
// Fixture
// =========================================================================

class CommandDeliveryPipeline : public ::testing::Test
{
  protected:
    void TearDown() override { Kernel::KernelTestAccess::resetSystem<Cfg>(); }
};

// =========================================================================
// Tests
// =========================================================================

TEST_F(CommandDeliveryPipeline, SingleCommandFieldsCorrect)
{
    // "1 2 50.0\n" → SET_FLOW (id=1), device 2, value 50.0
    InstrumentedApp<Cfg>    app;
    ScriptedStream          stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    stream.loadString("1 2 50.0\n");

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<Cfg>::init(0);
    // Use two ticks: first allows CommsTask to read and push,
    // second allows ControlTask to pop and dispatch.
    System<Cfg>::tick(0, SputterMicros(1000));
    System<Cfg>::tick(0, SputterMicros(2000));

    ASSERT_EQ(app.commandCount, 1u);
    EXPECT_EQ(static_cast<uint8_t>(app.commands[0].id),
              static_cast<uint8_t>(Cfg::CmdID::SET_FLOW));
    EXPECT_EQ(app.commands[0].targetDevice, 2u);
    EXPECT_FLOAT_EQ(app.commands[0].value, 50.0f);
}

TEST_F(CommandDeliveryPipeline, MalformedCommandNotDelivered)
{
    InstrumentedApp<Cfg>    app;
    ScriptedStream          stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    stream.loadString("NOTACOMMAND\n");

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<Cfg>::init(0);
    System<Cfg>::tick(0, SputterMicros(1000));
    System<Cfg>::tick(0, SputterMicros(2000));

    EXPECT_EQ(app.commandCount, 0u);
}

TEST_F(CommandDeliveryPipeline, OutOfRangeCommandIdRejected)
{
    // kMaxValidCommandID = 2; id 99 must be rejected by CommandParser.
    InstrumentedApp<Cfg>    app;
    ScriptedStream          stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    stream.loadString("99 0 0.0\n");

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<Cfg>::init(0);
    System<Cfg>::tick(0, SputterMicros(1000));
    System<Cfg>::tick(0, SputterMicros(2000));

    EXPECT_EQ(app.commandCount, 0u);
}

TEST_F(CommandDeliveryPipeline, QueueFullNoCrash)
{
    // Pre-fill queue to capacity, then push a new command through the stream.
    // CommsTask must send a NACK without crashing or blocking.
    InstrumentedApp<Cfg>    app;
    ScriptedStream          stream;
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<Cfg>::init(0);

    // Saturate the queue before any ticks so ControlTask cannot drain it yet.
    for (std::size_t i = 0; i < Cfg::kQueueCapacity; ++i)
    {
        Cmd cmd{Cfg::CmdID::SET_STATE, 0, static_cast<float>(i)};
        System<Cfg>::commandQueue().try_push(cmd);
    }

    // Load a valid command so CommsTask attempts a push that will fail.
    stream.loadString("1 0 99.0\n");

    // The system must not crash or block when the queue rejects the push.
    EXPECT_NO_FATAL_FAILURE({
        System<Cfg>::tick(0, SputterMicros(1000));
        System<Cfg>::tick(0, SputterMicros(2000));
    });
}

TEST_F(CommandDeliveryPipeline, EmptyStreamNoCommandDelivered)
{
    InstrumentedApp<Cfg>    app;
    FakeStreamReader        stream; // always returns 0 bytes
    AlwaysSafeSafetyMonitor monitor;
    ISafetyMonitor         *monitors[] = {&monitor};

    SystemBuilder<Cfg> builder(&app, monitors, 1);
    builder.setStream(&stream).setWatchdogKick(nullptr);
    ASSERT_TRUE(builder.build());

    System<Cfg>::init(0);
    System<Cfg>::tick(0, SputterMicros(1000));
    System<Cfg>::tick(0, SputterMicros(2000));

    EXPECT_EQ(app.commandCount, 0u);
}
