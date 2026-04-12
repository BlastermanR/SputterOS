/**
 * @file main_osNative.cpp
 * @brief OS-native entry point for the Lifecycle kernel state example.
 *
 * Demonstrates dual-core AMP scheduling with kernel state observation
 * and background gap utilisation:
 *
 * - **Kernel state tracking**: `MonitorTask` on Core 1 reads
 *   `System<Cfg>::kernelState()` each tick, logging the kernel's
 *   current lifecycle phase (CONFIGURED → INITIALIZING → RUNNING).
 *
 * - **Dual-core AMP scheduling**: `WorkerTask` (5 Hz) on Core 0 and
 *   `MonitorTask` (2 Hz) on Core 1 run independently via separate
 *   Cruncher instances.  Neither core's scheduling affects the other.
 *
 * - **Multi-core startup/shutdown barriers**: Uses `MultiCoreSync<2>`
 *   to synchronise init and teardown, exactly as a real dual-core MCU
 *   would require.
 *
 * - **Lifecycle callbacks**: `WorkerTask` implements `onSuspend()` and
 *   `onResume()` to demonstrate task lifecycle hooks (logged but not
 *   triggered in this example until `System::pause()/resume()` are
 *   implemented in Phase 4).
 *
 * Expected output (~3 seconds of running):
 * @code
 *   [<ts>][Comms]   [Core 1] State report #1: RUNNING
 *   [<ts>][Control] [Core 0] Worker tick #1
 *   [<ts>][Control] [Core 0] Worker tick #2
 *   [<ts>][Comms]   [Core 1] State report #2: RUNNING
 *   [<ts>][Control] [Core 0] Worker tick #3
 *   ...
 * @endcode
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "LifecycleConfig.h"
#include "LifecycleHAL.h"
#include "LifecycleOSAL.h"
#include "LifecycleStateMachine.h"
#include "MonitorTask.h"
#include "SharedState.h"
#include "TcpStreamServer.h"
#include "WorkerTask.h"

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
    using Cfg = Lifecycle::LifecycleConfig;
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
    Lifecycle::StdoutStreamReader      stdoutStream;
    ExamplesCommon::TcpStreamServer    tcpStream(tcpPort);
    Lifecycle::AlwaysSafeSafetyMonitor safetyMonitor;
    std::array<ISafetyMonitor *, 1>    monitors = {&safetyMonitor};

    // -- User application (permanently IDLE) --------------------------------
    Lifecycle::LifecycleApplication app;

    // -- Per-core telemetry loggers -----------------------------------------
    TelemetryLogger workerTelemetry;  // drained by Core 0
    TelemetryLogger monitorTelemetry; // drained by Core 1

    // -- User tasks ---------------------------------------------------------
    Lifecycle::WorkerTask  workerTask(workerTelemetry);   // Core 0, 5 Hz
    Lifecycle::MonitorTask monitorTask(monitorTelemetry); // Core 1, 2 Hz

    // -- Select active stream (TCP or stdout) -------------------------------
    SputterOS::IStream *activeStream = (tcpPort > 0) ? static_cast<SputterOS::IStream *>(&tcpStream)
                                                     : static_cast<SputterOS::IStream *>(&stdoutStream);
    if (tcpPort > 0 && !tcpStream.startAccept())
        return 1;

    // -- Build the kernel ---------------------------------------------------
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(activeStream);
    builder.setClockSource(Lifecycle::platformGetTimeMicros);
    builder.setWatchdogKick(nullptr);

    // Register user tasks on appropriate cores.
    // Kernel auto-places ScheduledControlTask → Core 0, ScheduledCommsTask → Core 1.
    builder.core(0).addScheduledTask(&workerTask);
    builder.core(1).addScheduledTask(&monitorTask);

    const BuildResult result = builder.build();
    if (!result)
    {
        std::fprintf(stderr, "SystemBuilder::build() failed: %s\n", result.error);
        return 1;
    }

    std::printf("--- Lifecycle Example: Dual-Core AMP Scheduling ---\n");
    std::printf("Kernel state after build(): %u\n", static_cast<unsigned>(System<Cfg>::kernelState()));

    // -- Retrieve sync handle before launching Core 1 -----------------------
    auto &sync = System<Cfg>::multiCoreSync();

    // -- Launch Core 1 thread -----------------------------------------------
    std::thread core1Thread(
        [&sync, &monitorTelemetry]()
        {
            sync.setInit(1);
            System<Cfg>::init(1);

            if (!sync.startupBarrier(1, ms{2000}))
            {
                return;
            }

            while (Lifecycle::g_running.load(std::memory_order_acquire))
            {
                const SputterMicros now = Lifecycle::platformGetTimeMicros();
                System<Cfg>::watchdog().kick(1, now);
                System<Cfg>::tick(1, now);
                monitorTelemetry.drain(stdoutWrite);
                std::this_thread::sleep_for(ms{1});
            }

            sync.shutdownBarrier(1, ms{2000});
        });

    // -- Core 0 (main thread) -----------------------------------------------
    sync.setInit(0);
    System<Cfg>::init(0);

    std::printf("Kernel state after init():  %u\n", static_cast<unsigned>(System<Cfg>::kernelState()));

    if (!sync.startupBarrier(0, ms{2000}))
    {
        std::fprintf(stderr, "Startup barrier timed out\n");
        core1Thread.join();
        return 1;
    }

    std::printf("Both cores running. Main loop for ~3 seconds...\n\n");

    // Run for ~3 seconds, or indefinitely when launched with --forever.
    static constexpr uint32_t kRunMs = 3000;
    using WallClock                  = std::chrono::steady_clock;
    const auto endAt                 = WallClock::now() + ms{kRunMs};

    while (forever || WallClock::now() < endAt)
    {
        const SputterMicros now = Lifecycle::platformGetTimeMicros();
        System<Cfg>::watchdog().kick(0, now);
        System<Cfg>::tick(0, now);
        workerTelemetry.drain(stdoutWrite);
        std::this_thread::sleep_for(ms{1});
    }

    // Signal shutdown to Core 1.
    Lifecycle::g_running.store(false, std::memory_order_release);

    sync.shutdownBarrier(0, ms{2000});
    core1Thread.join();

    // -- Final summary ------------------------------------------------------
    std::printf("\n--- Lifecycle Summary ---\n");
    std::printf("Worker iterations (Core 0): %u\n", workerTask.iterCount());
    std::printf("Final kernel state:         %u\n", static_cast<unsigned>(System<Cfg>::kernelState()));

    return 0;
}
