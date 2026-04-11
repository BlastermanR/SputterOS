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
    using Cfg = SensorPoll::SensorPollConfig;
    using namespace SputterOS;

    // -- HAL stubs ----------------------------------------------------------
    SensorPoll::StdoutStreamReader      stdoutStream;
    SensorPoll::AlwaysSafeSafetyMonitor safetyMonitor;
    std::array<ISafetyMonitor *, 1>     monitors = {&safetyMonitor};

    // -- User application (permanently IDLE) --------------------------------
    SensorPoll::SensorPollApplication app;

    // -- Diagnostics infrastructure -----------------------------------------
    TelemetryLogger telemetry;

    // -- Simulated ADC with 15 ms conversion delay --------------------------
    // Conversion delay (15 ms) < polling period (50 ms), so the ADC
    // completes within 1 extra tick after IO_PENDING.  The task
    // typically sees: tick 1 → start conversion → tick 2 → ready → read.
    SensorPoll::SimulatedADC adc(15'000, SensorPoll::platformGetTimeMicros);

    // -- User tasks ---------------------------------------------------------
    SensorPoll::AdcPollTask   adcTask(telemetry, adc);
    SensorPoll::SensorLogTask logTask(telemetry, adcTask);

    // -- Build the kernel ---------------------------------------------------
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stdoutStream);
    builder.setWatchdogKick(nullptr);

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

    // -- Initialise all tasks -----------------------------------------------
    System<Cfg>::init(0);

    // -- Main loop ----------------------------------------------------------
    // Run for ~1 second — yields ~20 ADC readings at 20 Hz.
    static constexpr uint32_t kRunMs = 1100;

    using WallClock  = std::chrono::steady_clock;
    const auto endAt = WallClock::now() + std::chrono::milliseconds{kRunMs};

    while (WallClock::now() < endAt)
    {
        const SputterMicros now = SensorPoll::platformGetTimeMicros();

        System<Cfg>::tick(0, now);
        telemetry.drain(stdoutWrite);

        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    // -- Final summary ------------------------------------------------------
    std::printf("\n--- SensorPoll Summary ---\n");
    std::printf("ADC readings:  %u\n", adcTask.readingCount());
    std::printf("Last reading:  %.1f mV\n", static_cast<double>(adcTask.lastReading()));

    return 0;
}
