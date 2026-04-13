/**
 * @file main_osNative.cpp
 * @brief OS-native entry point for the PingPong dual-core example.
 *
 * Constructs and runs a complete dual-core SputterOS microkernel on the
 * host OS using two `std::thread` instances to simulate Core 0 and Core 1.
 * The `System<Cfg>::run()` API internalises the full startup/shutdown
 * barrier lifecycle:
 *
 * @code
 *   SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
 *   builder.setStream(&stream);
 *   builder.setTelemetryDrain(stdoutWrite, nullptr);
 *   builder.setTelemetryMutex(&mutex);
 *   builder.addStopCondition(stopFn);
 *   builder.core(0).addScheduledTask(&pingTask);
 *   builder.core(1).addScheduledTask(&pongTask);
 *   builder.build();
 *
 *   std::thread core1([]() { System<Cfg>::run(1); });
 *   System<Cfg>::run(0);
 *   core1.join();
 * @endcode
 *
 * ### Task layout after build()
 *
 * | Core | Tasks (tick order) |
 * |------|-------------------|
 * | 0    | ScheduledControlTask → PingTask |
 * | 1    | ScheduledCommsTask → BackgroundDiagnosticsTask → PongTask |
 *
 * ### Counter exchange protocol
 *
 * `PingTask` and `PongTask` share three `std::atomic` variables in
 * `SharedCounter.h`:
 *
 *  - `g_counter`  — shared counter, incremented by one per turn.
 *  - `g_pingTurn` — token flag; `memory_order_release/acquire` pairs
 *                   ensure each core sees the peer's latest counter write.
 *  - `g_running`  — cleared by `PingTask` after `kMaxRounds` pings.
 *
 * Expected terminal output:
 * @code
 *   [Core 0] Ping #1 -> counter = 1
 *   [Core 1] Pong #1 -> counter = 2
 *   [Core 0] Ping #2 -> counter = 3
 *   [Core 1] Pong #2 -> counter = 4
 *   [Core 0] Ping #3 -> counter = 5
 *   [Core 1] Pong #3 -> counter = 6
 *   [Core 0] Ping #4 -> counter = 7
 *   [Core 1] Pong #4 -> counter = 8
 *   [Core 0] Ping #5 -> counter = 9
 * @endcode
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "PingPongConfig.h"
#include "PingPongHAL.h"
#include "PingPongOSAL.h"
#include "PingPongStateMachine.h"
#include "PingTask.h"
#include "PongTask.h"
#include "SharedCounter.h"
#include "TcpStreamServer.h"

#include "TimedMutexAdapter.h"
#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/System.h"
#include "sputteros/kernel/interfaces/ISafetyMonitor.h"
#include "sputteros/osal/SputterTime.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <thread>

/// @brief Drain callback — writes telemetry text to stdout.
static void stdoutWrite(const uint8_t *data, std::size_t len, void * /*ctx*/) { std::fwrite(data, 1, len, stdout); }

int main(int argc, char *argv[])
{
    using Cfg = PingPong::PingPongConfig;
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
    PingPong::StdoutStreamReader      stdoutStream;
    ExamplesCommon::TcpStreamServer   tcpStream(tcpPort);
    PingPong::AlwaysSafeSafetyMonitor safetyMonitor;
    std::array<ISafetyMonitor *, 1>   monitors = {&safetyMonitor};

    // -- User application (permanently IDLE — no real process) --------------
    PingPong::PingPongApplication app;

    // -- Mutex for kernel-owned TelemetryLogger (shared across cores) -------
    PingPong::TimedMutexAdapter telemetryMutex;

    // -- User tasks (write to kernel-owned TelemetryLogger) -----------------
    PingPong::PingTask pingTask(System<Cfg>::telemetryLogger()); // Core 0
    PingPong::PongTask pongTask(System<Cfg>::telemetryLogger()); // Core 1

    // -- Select active stream (TCP or stdout) -------------------------------
    SputterOS::IStream *activeStream = (tcpPort > 0) ? static_cast<SputterOS::IStream *>(&tcpStream)
                                                     : static_cast<SputterOS::IStream *>(&stdoutStream);
    if (tcpPort > 0 && !tcpStream.startAccept())
        return 1;

    // -- Build the kernel ---------------------------------------------------
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(activeStream);
    builder.setClockSource(PingPong::platformGetTimeMicros);
    builder.setWatchdogKick(nullptr);
    builder.setTelemetryDrain(stdoutWrite, nullptr);
    builder.setTelemetryMutex(&telemetryMutex);

    // Stop condition: PingTask clears g_running after kMaxRounds.
    if (!forever)
    {
        builder.addStopCondition([]() -> bool { return !PingPong::g_running.load(std::memory_order_acquire); });
    }

    // Register user tasks on the appropriate cores.
    builder.core(0).addScheduledTask(&pingTask);
    builder.core(1).addScheduledTask(&pongTask);

    const BuildResult result = builder.build();
    if (!result)
    {
        std::fprintf(stderr, "SystemBuilder::build() failed: %s\n", result.error);
        return 1;
    }

    // -- Launch Core 1 and run Core 0 (both block until stop condition) -----
    std::thread core1Thread([]() { System<Cfg>::run(1); });
    System<Cfg>::run(0);
    core1Thread.join();

    return 0;
}
