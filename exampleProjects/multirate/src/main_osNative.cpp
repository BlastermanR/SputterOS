/**
 * @file main_osNative.cpp
 * @brief OS-native entry point for the MultiRate scheduling example.
 *
 * Demonstrates core AMP scheduling features on a single-core system:
 *
 * - **Multi-rate dispatch**: `FastSampleTask` (100 Hz) and `SlowReportTask`
 *   (2 Hz) share Core 0 with kernel tasks.  The Cruncher dispatches
 *   the fast task ~50× more often, with RMS priority ordering ensuring
 *   the shorter-period task always runs first when both are due.
 *
 * - **Background gap utilisation**: `IdleCounterTask` runs via the
 *   SystemScheduler whenever the Cruncher has no READY slots, filling
 *   idle CPU time without affecting deterministic task deadlines.
 *
 * - **Data flow between rates**: `FastSampleTask` accumulates simulated
 *   sensor readings at 100 Hz; `SlowReportTask` drains the accumulator
 *   at 2 Hz and logs a summary.
 *
 * Expected output (5 reports over ~2.5 seconds):
 * @code
 *   [<ts>][System] Report #1 | samples=50 accum=1225
 *   [<ts>][System] Report #2 | samples=100 accum=1225
 *   [<ts>][System] Report #3 | samples=150 accum=1225
 *   [<ts>][System] Report #4 | samples=200 accum=1225
 *   [<ts>][System] Report #5 | samples=250 accum=1225
 * @endcode
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "MultirateConfig.h"
#include "MultirateHAL.h"
#include "MultirateOSAL.h"
#include "MultirateStateMachine.h"

#include "FastSampleTask.h"
#include "IdleCounterTask.h"
#include "SlowReportTask.h"

#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/System.h"
#include "sputteros/kernel/interfaces/ISafetyMonitor.h"
#include "sputteros/osal/SputterTime.h"
#include "sputteros/utils/logging/TelemetryLogger.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <thread>

/// @brief Drain callback — writes telemetry text to stdout.
static void stdoutWrite(const uint8_t *data, std::size_t len, void * /*ctx*/) { std::fwrite(data, 1, len, stdout); }

int main()
{
    using Cfg = Multirate::MultirateConfig;
    using namespace SputterOS;

    // -- HAL stubs ----------------------------------------------------------
    Multirate::StdoutStreamReader      stdoutStream;
    Multirate::AlwaysSafeSafetyMonitor safetyMonitor;
    std::array<ISafetyMonitor *, 1>    monitors = {&safetyMonitor};

    // -- User application (permanently IDLE) --------------------------------
    Multirate::MultirateApplication app;

    // -- Diagnostics infrastructure -----------------------------------------
    TelemetryLogger telemetry;

    // -- User tasks ---------------------------------------------------------
    Multirate::FastSampleTask  fastTask;
    Multirate::SlowReportTask  slowTask(telemetry, fastTask);
    Multirate::IdleCounterTask idleTask(telemetry);

    // -- Build the kernel ---------------------------------------------------
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stdoutStream);
    builder.setWatchdogKick(nullptr);

    // Register scheduled tasks on Core 0.
    // The Cruncher auto-assigns RMS priority: FastSampleTask (10 ms period)
    // gets higher priority than SlowReportTask (500 ms period).
    builder.core(0).addScheduledTask(&fastTask);
    builder.core(0).addScheduledTask(&slowTask);

    // Register background task — runs in gap time on any core.
    builder.addBackgroundTask(&idleTask);

    const BuildResult result = builder.build();
    if (!result)
    {
        std::fprintf(stderr, "SystemBuilder::build() failed: %s\n", result.error);
        return 1;
    }

    // -- Initialise all tasks -----------------------------------------------
    System<Cfg>::init(0);

    // -- Main loop ----------------------------------------------------------
    // Run for 5 slow reports (5 × 500 ms) plus a drain buffer.
    static constexpr uint32_t kNumReports = 5;
    static constexpr uint32_t kDrainMs    = 200;

    using WallClock  = std::chrono::steady_clock;
    const auto endAt = WallClock::now() + std::chrono::milliseconds{
                                              kNumReports * (Multirate::SlowReportTask::kPeriodUs / 1000) + kDrainMs};

    while (WallClock::now() < endAt)
    {
        const SputterMicros now = Multirate::platformGetTimeMicros();

        // Tick all Core 0 tasks via the Cruncher + SystemScheduler.
        System<Cfg>::tick(0, now);

        // Drain buffered telemetry to stdout.
        telemetry.drain(stdoutWrite);

        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    // -- Final summary ------------------------------------------------------
    std::printf("\n--- MultiRate Summary ---\n");
    std::printf("Fast samples:         %u\n", fastTask.sampleCount());
    std::printf("Background dispatches: %u\n", idleTask.dispatchCount());

    return 0;
}
