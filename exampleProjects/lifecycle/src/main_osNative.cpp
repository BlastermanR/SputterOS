/**
 * @file main_osNative.cpp
 * @brief OS-native entry point for the Lifecycle kernel state example.
 *
 * Demonstrates dual-core AMP scheduling with kernel state observation
 * using the `System<Cfg>::run()` blocking API:
 *
 * @code
 *   SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
 *   builder.setStream(&stream);
 *   builder.setTelemetryDrain(stdoutWrite, nullptr);
 *   builder.setTelemetryMutex(&mutex);
 *   builder.addStopCondition(stopFn);
 *   builder.core(0).addScheduledTask(&workerTask);
 *   builder.core(1).addScheduledTask(&monitorTask);
 *   builder.build();
 *
 *   std::thread core1([]() { System<Cfg>::run(1); });
 *   System<Cfg>::run(0);
 *   core1.join();
 * @endcode
 *
 * - **Kernel state tracking**: `MonitorTask` on Core 1 reads
 *   `System<Cfg>::kernelState()` each tick, logging the kernel's
 *   current lifecycle phase (CONFIGURED → INITIALIZING → RUNNING).
 *
 * - **Dual-core AMP scheduling**: `WorkerTask` (5 Hz) on Core 0 and
 *   `MonitorTask` (2 Hz) on Core 1 run independently via separate
 *   Cruncher instances.  Neither core's scheduling affects the other.
 *
 * - **Multi-core startup/shutdown barriers**: Managed internally by
 *   `System<Cfg>::run()` — the caller never touches `MultiCoreSync`.
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
#include "WorkerTask.h"
#include "common/hal/TcpStreamServer.h"

#include "TimedMutexAdapter.h"
#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/System.h"
#include "sputteros/kernel/interfaces/ISafetyMonitor.h"
#include "sputteros/osal/SputterTime.h"

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

    // -- Mutex for kernel-owned TelemetryLogger (shared across cores) -------
    Lifecycle::TimedMutexAdapter telemetryMutex;

    // -- User tasks (write to kernel-owned TelemetryLogger) -----------------
    Lifecycle::WorkerTask  workerTask(System<Cfg>::telemetryLogger());  // Core 0, 5 Hz
    Lifecycle::MonitorTask monitorTask(System<Cfg>::telemetryLogger()); // Core 1, 2 Hz

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
    builder.setTelemetryDrain(stdoutWrite, nullptr);
    builder.setTelemetryMutex(&telemetryMutex);

    // Stop condition: wallclock timeout OR g_running cleared externally.
    if (!forever)
    {
        static constexpr uint32_t kRunMs = 3000;
        builder.addStopCondition(
            []() -> bool
            {
                using Clock             = std::chrono::steady_clock;
                static const auto endAt = Clock::now() + std::chrono::milliseconds{kRunMs};
                return Clock::now() >= endAt;
            });
    }
    builder.addStopCondition([]() -> bool { return !Lifecycle::g_running.load(std::memory_order_acquire); });

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

    // -- Launch Core 1 and run Core 0 (both block until stop condition) -----
    std::thread core1Thread([]() { System<Cfg>::run(1); });
    System<Cfg>::run(0);
    core1Thread.join();

    // -- Final summary ------------------------------------------------------
    std::printf("\n--- Lifecycle Summary ---\n");
    std::printf("Worker iterations (Core 0): %u\n", workerTask.iterCount());
    std::printf("Final kernel state:         %u\n", static_cast<unsigned>(System<Cfg>::kernelState()));

    return 0;
}
