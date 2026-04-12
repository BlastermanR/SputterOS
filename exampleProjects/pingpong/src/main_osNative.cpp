/**
 * @file main_osNative.cpp
 * @brief OS-native entry point for the PingPong dual-core example.
 *
 * Constructs and runs a complete dual-core SputterOS microkernel on the
 * host OS using two `std::thread` instances to simulate Core 0 and Core 1.
 * The full startup/shutdown barrier lifecycle is exercised:
 *
 * @code
 *   // ── Build ──────────────────────────────────────────────────────────
 *   PingPongApplication app;
 *   AlwaysSafeSafetyMonitor monitor;
 *   std::array<ISafetyMonitor*, 1> monitors = {&monitor};
 *
 *   SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
 *   builder.setStream(&stream);
 *   builder.setWatchdogKick(nullptr);
 *   builder.core(0).addScheduledTask(&pingTask);   // Core 0 user task
 *   builder.core(1).addScheduledTask(&pongTask);   // Core 1 user task
 *   builder.build();
 *
 *   // ── Core 1 thread ──────────────────────────────────────────────────
 *   std::thread core1Thread([&]() {
 *       sync.setInit(1);
 *       System<Cfg>::init(1);
 *       sync.startupBarrier(1, 2000ms);   // waits for Core 0 READY
 *       while (g_running) { System<Cfg>::tick(1, now); }
 *       sync.shutdownBarrier(1, 2000ms);
 *   });
 *
 *   // ── Core 0 (main thread) ──────────────────────────────────────────
 *   sync.setInit(0);
 *   System<Cfg>::init(0);
 *   sync.startupBarrier(0, 2000ms);       // waits for Core 1 READY
 *   while (g_running) { System<Cfg>::tick(0, now); }
 *   sync.shutdownBarrier(0, 2000ms);
 *   core1Thread.join();
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
    using Cfg = PingPong::PingPongConfig;
    using namespace SputterOS;
    using ms = std::chrono::milliseconds;

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

    // -- Per-core telemetry loggers -----------------------------------------
    // Each core owns its logger to avoid concurrent writes to a shared buffer.
    // Both drain to stdout via the same stdoutWrite callback; stdio is
    // internally thread-safe on all supported host platforms.
    TelemetryLogger pingTelemetry; // drained by Core 0 main loop
    TelemetryLogger pongTelemetry; // drained by Core 1 thread loop

    // -- User tasks ---------------------------------------------------------
    PingPong::PingTask pingTask(pingTelemetry); // Core 0
    PingPong::PongTask pongTask(pongTelemetry); // Core 1

    // -- Select active stream (TCP or stdout) -------------------------------
    SputterOS::IStream *activeStream = (tcpPort > 0)
                                           ? static_cast<SputterOS::IStream *>(&tcpStream)
                                           : static_cast<SputterOS::IStream *>(&stdoutStream);
    if (tcpPort > 0 && !tcpStream.startAccept())
        return 1;

    // -- Build the kernel ---------------------------------------------------
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(activeStream);
    builder.setClockSource(PingPong::platformGetTimeMicros);
    builder.setWatchdogKick(nullptr);

    // Register user tasks on the appropriate cores.
    // Kernel tasks (ScheduledControlTask, ScheduledCommsTask, BackgroundDiagnosticsTask) are
    // auto-placed by build(): ScheduledControlTask → Core 0,
    // ScheduledCommsTask + BackgroundDiagnosticsTask → Core 1.
    builder.core(0).addScheduledTask(&pingTask);
    builder.core(1).addScheduledTask(&pongTask);

    const BuildResult result = builder.build();
    if (!result)
    {
        std::fprintf(stderr, "SystemBuilder::build() failed: %s\n", result.error);
        return 1;
    }

    // -- Retrieve the sync handle before launching Core 1 ------------------
    auto &sync = System<Cfg>::multiCoreSync();

    // -- Launch Core 1 thread -----------------------------------------------
    // Mirrors the role of `multicore_launch_core1()` on the RP2350.
    // The lambda captures by reference; it is safe because main() waits
    // for the thread to join before any captured objects are destroyed.
    std::thread core1Thread(
        [&sync, &pongTelemetry, forever]()
        {
            // Signal that Core 1 has started local initialisation.
            sync.setInit(1);

            // Initialise all tasks registered on Core 1.
            System<Cfg>::init(1);

            // Wait until Core 0 is also READY (or timeout → error).
            if (!sync.startupBarrier(1, ms{2000}))
            {
                return; // startupBarrier sets ERROR state on timeout
            }

            // Core 1 tick loop — runs until PingTask clears g_running,
            // or indefinitely when --forever is set.
            while (forever || PingPong::g_running.load(std::memory_order_acquire))
            {
                const SputterMicros now = PingPong::platformGetTimeMicros();
                System<Cfg>::watchdog().kick(1, now);
                System<Cfg>::tick(1, now);
                pongTelemetry.drain(stdoutWrite);
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }

            // Orderly shutdown — signal and wait for Core 0.
            if (!forever)
            {
                sync.shutdownBarrier(1, ms{2000});
            }
        });

    // -- Core 0 startup sequence --------------------------------------------
    // Signal that Core 0 has started local initialisation.
    sync.setInit(0);

    // Initialise all tasks registered on Core 0.
    System<Cfg>::init(0);

    // Wait until Core 1 is also READY (or timeout → error).
    if (!sync.startupBarrier(0, ms{2000}))
    {
        std::fprintf(stderr, "Startup barrier timed out on Core 0\n");
        core1Thread.join();
        return 1;
    }

    // -- Core 0 tick loop ---------------------------------------------------
    while (forever || PingPong::g_running.load(std::memory_order_acquire))
    {
        const SputterMicros now = PingPong::platformGetTimeMicros();
        System<Cfg>::watchdog().kick(0, now);
        System<Cfg>::tick(0, now);
        pingTelemetry.drain(stdoutWrite);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    // -- Core 0 orderly shutdown --------------------------------------------
    if (!forever)
    {
        sync.shutdownBarrier(0, ms{2000});
    }
    core1Thread.join();

    return 0;
}
