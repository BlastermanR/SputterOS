/**
 * @file main_osNative.cpp
 * @brief OS-native entry point for the HeartBeat system test.
 *
 * Constructs and runs a complete SputterOS microkernel on the host OS
 * using the `SystemBuilder` declarative API and `System` runtime API:
 *
 * @code
 *   HeartbeatApplication app;
 *   AlwaysSafeSafetyMonitor monitor;
 *   std::array<ISafetyMonitor*, 1> monitors = {&monitor};
 *
 *   SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
 *   builder.setStream(&stdoutStream);
 *   builder.setWatchdogKick(nullptr);
 *   builder.core(0).addScheduledTask(&pulseTask);
 *   builder.build();
 *
 *   System<Cfg>::init(0);
 *   while (running) { System<Cfg>::tick(0, now); }
 * @endcode
 *
 * The kernel internally creates and manages:
 *  - `ScheduledControlTask<HeartbeatConfig>`  — safety loop (Core 0)
 *  - `ScheduledCommsTask<HeartbeatConfig>`    — CLI bridge over stdout stream (Core 0)
 *  - `BackgroundDiagnosticsTask`               — health monitor (Core 0)
 *
 * The user adds one custom task:
 *  - `PulseTask` — emits "Pulse #N" every 500 ms via TelemetryLogger
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */

#include "HeartbeatConfig.h"
#include "HeartbeatHAL.h"
#include "HeartbeatOSAL.h"
#include "HeartbeatStateMachine.h"
#include "PulseTask.h"
#include "TcpStreamServer.h"

#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/System.h"
#include "sputteros/kernel/interfaces/ISafetyMonitor.h"
#include "sputteros/osal/SputterTime.h"
#include "sputteros/utils/logging/TelemetryLogger.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

/// @brief Drain callback — writes telemetry text to stdout.
static void stdoutWrite(const uint8_t *data, std::size_t len, void * /*ctx*/) { std::fwrite(data, 1, len, stdout); }

int main(int argc, char *argv[])
{
    using Cfg = Heartbeat::HeartbeatConfig;
    using namespace SputterOS;

    // Parse flags: --forever, --tcp-port <N>
    bool     forever = false;
    uint16_t tcpPort = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--forever") == 0)
            forever = true;
        else if (std::strcmp(argv[i], "--tcp-port") == 0 && i + 1 < argc)
            tcpPort = static_cast<uint16_t>(std::atoi(argv[++i]));
    }

    // -- HAL stubs ----------------------------------------------------------
    Heartbeat::StdoutStreamReader      stdoutStream;
    ExamplesCommon::TcpStreamServer    tcpStream(tcpPort);
    Heartbeat::AlwaysSafeSafetyMonitor safetyMonitor;
    std::array<ISafetyMonitor *, 1>    monitors = {&safetyMonitor};

    // -- User application (permanently IDLE — no real process) --------------
    Heartbeat::HeartbeatApplication app;

    // -- Diagnostics infrastructure -----------------------------------------
    TelemetryLogger telemetry;
    // ErrorLogger and MemoryProfiler are now kernel-owned by SystemBuilder.
    // Per-task timing is handled by kernel-owned TaskTimers.

    // -- Custom user task ---------------------------------------------------
    Heartbeat::PulseTask pulseTask(telemetry);

    // -- Select active stream (TCP or stdout) -------------------------------
    SputterOS::IStream *activeStream = (tcpPort > 0) ? static_cast<SputterOS::IStream *>(&tcpStream)
                                                     : static_cast<SputterOS::IStream *>(&stdoutStream);
    if (tcpPort > 0 && !tcpStream.startAccept())
        return 1;

    // -- Build the kernel ---------------------------------------------------
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(activeStream);
    builder.setClockSource(Heartbeat::platformGetTimeMicros);
    builder.setWatchdogKick(nullptr);

    // Add the custom user task to core 0
    builder.core(0).addScheduledTask(&pulseTask);

    // Validate and finalise
    const BuildResult result = builder.build();
    if (!result)
    {
        return 1;
    }

    // -- Initialise all tasks via the System --------------------------------
    System<Cfg>::init(0);

    // -- Main loop ----------------------------------------------------------
    // Run until kNumPulses heartbeats have been emitted (one every 500 ms),
    // or indefinitely when launched with --forever.
    static constexpr uint32_t kNumPulses = 5;
    static constexpr uint32_t kDrainMs   = 100;

    using WallClock  = std::chrono::steady_clock;
    const auto endAt = WallClock::now() +
                       std::chrono::milliseconds{(kNumPulses - 1) * Heartbeat::PulseTask::kPulseIntervalMs + kDrainMs};

    while (forever || WallClock::now() < endAt)
    {
        const SputterMicros now = Heartbeat::platformGetTimeMicros();

        // Tick every task on core 0 via the System
        System<Cfg>::tick(0, now);

        // Drain any buffered telemetry lines to stdout each cycle.
        telemetry.drain(stdoutWrite);

        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    return 0;
}
