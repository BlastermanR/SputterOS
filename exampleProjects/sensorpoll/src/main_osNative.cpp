/**
 * @file main_osNative.cpp
 * @brief OS-native entry point for the SensorPoll IO_PENDING example.
 *
 * Demonstrates the non-blocking IO pattern (IO_PENDING) central to
 * AMP scheduling:
 *
 * - **IO_PENDING yield**: `AdcPollTask` starts a simulated ADC
 *   conversion and yields.  The Cruncher marks the slot as
 *   `IO_PENDING`.  On the next activation the task polls for
 *   completion — no blocking, no spin-waiting.
 *
 * - **Fault resilience**: If the ADC hangs beyond `kMaxIoRetries`
 *   polls, the task logs a fault and resets instead of blocking
 *   forever.  Safety evaluation is never stalled.
 *
 * - **Background data drain**: `SensorLogTask` runs in
 *   SystemScheduler gap time, summarising accumulated readings
 *   without affecting the deterministic polling schedule.
 *
 * Expected output (20 readings over ~1 second at 20 Hz):
 * @code
 *   [<ts>][Control] ADC #1 = 0.000 mV
 *   [<ts>][Control] ADC #2 = 37.000 mV
 *   [<ts>][System]  Sensor log: 5 readings, last=148.000 mV
 *   [<ts>][Control] ADC #3 = 74.000 mV
 *   ...
 * @endcode
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "SensorPollConfig.h"
#include "SensorPollHAL.h"
#include "SensorPollOSAL.h"
#include "SensorPollStateMachine.h"

#include "AdcPollTask.h"
#include "SensorLogTask.h"
#include "TcpStreamServer.h"

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
    using Cfg = SensorPoll::SensorPollConfig;
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
    SensorPoll::StdoutStreamReader      stdoutStream;
    ExamplesCommon::TcpStreamServer     tcpStream(tcpPort);
    SensorPoll::AlwaysSafeSafetyMonitor safetyMonitor;
    std::array<ISafetyMonitor *, 1>     monitors = {&safetyMonitor};

    // -- User application (permanently IDLE) --------------------------------
    SensorPoll::SensorPollApplication app;

    // -- Simulated ADC with 15 ms conversion delay --------------------------
    SensorPoll::SimulatedADC adc(15'000, SensorPoll::platformGetTimeMicros);

    // -- User tasks (write to kernel-owned TelemetryLogger) -----------------
    SensorPoll::AdcPollTask   adcTask(System<Cfg>::telemetryLogger(), adc);
    SensorPoll::SensorLogTask logTask(System<Cfg>::telemetryLogger(), adcTask);

    // -- Select active stream (TCP or stdout) -------------------------------
    SputterOS::IStream *activeStream = (tcpPort > 0) ? static_cast<SputterOS::IStream *>(&tcpStream)
                                                     : static_cast<SputterOS::IStream *>(&stdoutStream);
    if (tcpPort > 0 && !tcpStream.startAccept())
        return 1;

    // -- Stop condition: run for ~1 second unless --forever ------------------
    static constexpr uint32_t kRunMs = 1100;

    using WallClock     = std::chrono::steady_clock;
    static auto s_endAt = WallClock::now() + std::chrono::milliseconds{kRunMs};

    // -- Build the kernel ---------------------------------------------------
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(activeStream);
    builder.setClockSource(SensorPoll::platformGetTimeMicros);
    builder.setWatchdogKick(nullptr);
    builder.setTelemetryDrain(stdoutWrite, nullptr);

    // Register stop condition (skipped when --forever)
    if (!forever)
    {
        builder.addStopCondition([]() -> bool { return WallClock::now() >= s_endAt; });
    }

    // Register the ADC polling task as a scheduled task.
    builder.core(0).addScheduledTask(&adcTask);

    // Register the sensor log as a background task.
    builder.addBackgroundTask(&logTask);

    const BuildResult result = builder.build();
    if (!result)
    {
        std::fprintf(stderr, "SystemBuilder::build() failed: %s\n", result.error);
        return 1;
    }

    // -- Run the kernel (blocks until stop condition fires) -----------------
    System<Cfg>::run(0);

    // -- Final summary ------------------------------------------------------
    std::printf("\n--- SensorPoll Summary ---\n");
    std::printf("ADC readings:  %u\n", adcTask.readingCount());
    std::printf("Last reading:  %.1f mV\n", static_cast<double>(adcTask.lastReading()));

    return 0;
}
