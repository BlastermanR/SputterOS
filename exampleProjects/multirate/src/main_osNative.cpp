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
#include "common/hal/TcpStreamServer.h"

#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/System.h"
#include "sputteros/kernel/interfaces/ISafetyMonitor.h"
#include "sputteros/osal/SputterTime.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>

/// @brief Drain callback — writes telemetry text to stdout.
static void stdoutWrite(const uint8_t *data, std::size_t len, void * /*ctx*/) { std::fwrite(data, 1, len, stdout); }

int main(int argc, char *argv[])
{
    using Cfg = Multirate::MultirateConfig;
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
    Multirate::StdoutStreamReader      stdoutStream;
    ExamplesCommon::TcpStreamServer    tcpStream(tcpPort);
    Multirate::AlwaysSafeSafetyMonitor safetyMonitor;
    std::array<ISafetyMonitor *, 1>    monitors = {&safetyMonitor};

    // -- User application (permanently IDLE) --------------------------------
    Multirate::MultirateApplication app;

    // -- User tasks (write to kernel-owned TelemetryLogger) -----------------
    Multirate::FastSampleTask  fastTask;
    Multirate::SlowReportTask  slowTask(System<Cfg>::telemetryLogger(), fastTask);
    Multirate::IdleCounterTask idleTask(System<Cfg>::telemetryLogger());

    // -- Select active stream (TCP or stdout) -------------------------------
    SputterOS::IStream *activeStream = (tcpPort > 0) ? static_cast<SputterOS::IStream *>(&tcpStream)
                                                     : static_cast<SputterOS::IStream *>(&stdoutStream);
    if (tcpPort > 0 && !tcpStream.startAccept())
        return 1;

    // -- Stop condition: run for 5 slow reports unless --forever -------------
    static constexpr uint32_t kNumReports = 5;
    static constexpr uint32_t kDrainMs    = 200;

    using WallClock = std::chrono::steady_clock;
    static auto s_endAt =
        WallClock::now() +
        std::chrono::milliseconds{kNumReports * (Multirate::SlowReportTask::kPeriodUs / 1000) + kDrainMs};

    // -- Build the kernel ---------------------------------------------------
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(activeStream);
    builder.setClockSource(Multirate::platformGetTimeMicros);
    builder.setWatchdogKick(nullptr);
    builder.setTelemetryDrain(stdoutWrite, nullptr);

    // Register stop condition (skipped when --forever)
    if (!forever)
    {
        builder.addStopCondition([]() -> bool { return WallClock::now() >= s_endAt; });
    }

    // Register scheduled tasks on Core 0.
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

    // -- Run the kernel (blocks until stop condition fires) -----------------
    System<Cfg>::run(0);

    // -- Final summary ------------------------------------------------------
    std::printf("\n--- MultiRate Summary ---\n");
    std::printf("Fast samples:         %u\n", fastTask.sampleCount());
    std::printf("Background dispatches: %u\n", idleTask.dispatchCount());

    return 0;
}
