/**
 * @file main_osNative.cpp
 * @brief OS-native entry point for the CrunchLoop example.
 *
 * Demonstrates dual-core CRUNCH dispatch mode with a tight-loop
 * `ICrunchTask` on Core 1 communicating with a standard FLAT_LOOP
 * `IUserApplication` on Core 0 through `AtomicDoubleBuffer` channels.
 *
 * @code
 *   SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
 *   builder.setStream(&stream);
 *   builder.setTelemetryDrain(stdoutWrite, nullptr);
 *   builder.setTelemetryMutex(&mutex);
 *   builder.core(1).setCrunchTask(&servo);
 *   builder.build();
 *
 *   std::thread core1([]() { System<Cfg>::run(1); });
 *   System<Cfg>::run(0);
 *   core1.join();
 * @endcode
 *
 * - **Core 0 (FLAT_LOOP):** Runs `ScheduledControlTask` at 10 Hz.
 *   `CrunchLoopApp::tick()` publishes incrementing servo targets via
 *   `AtomicDoubleBuffer<TargetState>`.
 *
 * - **Core 1 (CRUNCH):** Runs `SimulatedServoTask` in a tight loop
 *   (~6 kHz).  Each `crunch()` iteration reads the latest target,
 *   simulates blocking SPI, and writes encoder state back.
 *
 * - **Cross-core data flow:** Two `AtomicDoubleBuffer` channels —
 *   `targetBuf` (Core 0 → Core 1) and `stateBuf` (Core 1 → Core 0).
 *
 * Expected output (~3 seconds of running):
 * @code
 *   --- CrunchLoop Example: Dual-Core CRUNCH Mode ---
 *   [Final] App targets published: 30
 *   [Final] Servo iterations:      ~18000
 *   [Final] Servo aborted:         no
 * @endcode
 *
 * @author Ryan Massie (rmassie)
 * @date 4/14/2026
 */

#include "CrunchLoopApp.h"
#include "CrunchLoopConfig.h"
#include "CrunchLoopHAL.h"
#include "CrunchLoopOSAL.h"
#include "SimulatedServoTask.h"

#include "TimedMutexAdapter.h"
#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/System.h"
#include "sputteros/kernel/interfaces/ISafetyMonitor.h"
#include "sputteros/osal/SputterTime.h"
#include "sputteros/osal/sync/AtomicDoubleBuffer.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

/// @brief Drain callback — writes telemetry text to stdout.
static void stdoutWrite(const uint8_t *data, std::size_t len, void * /*ctx*/) { std::fwrite(data, 1, len, stdout); }

int main(int argc, char *argv[])
{
    using Cfg = CrunchLoop::CrunchLoopConfig;
    using namespace SputterOS;

    // Parse flags: --forever
    bool forever = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--forever") == 0)
            forever = true;
    }

    // -- HAL stubs ----------------------------------------------------------
    CrunchLoop::StdoutStreamReader      stdoutStream;
    CrunchLoop::AlwaysSafeSafetyMonitor safetyMonitor;
    std::array<ISafetyMonitor *, 1>     monitors = {&safetyMonitor};

    // -- Cross-core double buffers ------------------------------------------
    AtomicDoubleBuffer<CrunchLoop::TargetState>  targetBuf;
    AtomicDoubleBuffer<CrunchLoop::EncoderState> stateBuf;

    // -- User application (Core 0, FLAT_LOOP) — publishes servo targets -----
    CrunchLoop::CrunchLoopApp app(targetBuf);

    // -- Crunch task (Core 1, CRUNCH) — tight-loop servo simulation ---------
    CrunchLoop::SimulatedServoTask servo(targetBuf, stateBuf);

    // -- Mutex for kernel-owned TelemetryLogger (shared across cores) -------
    CrunchLoop::TimedMutexAdapter telemetryMutex;

    // -- Build the kernel ---------------------------------------------------
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stdoutStream);
    builder.setClockSource(CrunchLoop::platformGetTimeMicros);
    builder.setWatchdogKick(nullptr);
    builder.setTelemetryDrain(stdoutWrite, nullptr);
    builder.setTelemetryMutex(&telemetryMutex);

    // Stop condition: wallclock timeout.
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

    // Register crunch task on Core 1 (CRUNCH dispatch mode).
    builder.core(1).setCrunchTask(&servo);

    const BuildResult result = builder.build();
    if (!result)
    {
        std::fprintf(stderr, "SystemBuilder::build() failed: %s\n", result.error);
        return 1;
    }

    std::printf("--- CrunchLoop Example: Dual-Core CRUNCH Mode ---\n");

    // -- Launch Core 1 (CRUNCH) and run Core 0 (FLAT_LOOP) -----------------
    std::thread core1Thread([]() { System<Cfg>::run(1); });
    System<Cfg>::run(0);
    core1Thread.join();

    // -- Final summary ------------------------------------------------------
    auto finalState = stateBuf.read();
    std::printf("\n--- CrunchLoop Summary ---\n");
    std::printf("App targets published: %u\n", app.seqNum());
    std::printf("Servo iterations:      %u\n", servo.iterationCount());
    std::printf("Servo aborted:         %s\n", servo.wasAborted() ? "yes" : "no");
    std::printf("Final encoder count:   %u\n", finalState.iterationCount);

    return 0;
}
