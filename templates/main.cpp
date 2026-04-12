/**
 * @file main.cpp
 * @brief SputterOS application entry point template — copy and customise.
 *
 * This file demonstrates the complete setup sequence for a SputterOS
 * application. Copy it into your project and fill in the marked sections.
 *
 * ### Setup Flow
 * 1. Implement your IUserApplication (your process logic)
 * 2. Implement your ISafetyMonitor(s) (hardware safety checks)
 * 3. Implement your IStream (serial/USB byte stream)
 * 4. Wire everything through SystemBuilder
 * 5. Run the main loop
 *
 * ### Single-Core vs Dual-Core
 * This template defaults to single-core. For dual-core (e.g., RP2040):
 * - Set `kCoreCount = 2` in your config
 * - Uncomment the dual-core sections marked with "DUAL-CORE"
 * - See exampleProjects/lifecycle/ for a working dual-core example
 *
 * @author YourName
 * @date YYYY/MM/DD
 */

// ── Your Project Headers ────────────────────────────────────────────────────
#include "MyProjectConfig.h"  // Your configuration struct

// ── SputterOS Headers ───────────────────────────────────────────────────────
// Option A: Include everything (convenient for getting started)
// #include "sputteros/SputterOS.h"

// Option B: Include only what you need (recommended for production)
#include "sputteros/Builder.h"     // SystemBuilder
#include "sputteros/Kernel.h"      // System, ISafetyMonitor, IUserApplication
#include "sputteros/OSAL.h"        // SputterTime, tasks, sync
#include "sputteros/HAL.h"         // IStream
#include "sputteros/Utils.h"       // TelemetryLogger

// ── Standard Library ────────────────────────────────────────────────────────
#include <array>

using Cfg = MyProject::MyProjectConfig;
using namespace SputterOS;

// =========================================================================
//  STEP 1: Implement Your Application
// =========================================================================

/**
 * @brief Your process control logic — called every control tick.
 *
 * Implement the IUserApplication interface to define what happens each
 * cycle. The kernel calls tick() at the configured control rate (default
 * 100 Hz) after safety checks pass and commands are drained.
 */
class MyApplication : public IUserApplication<Cfg>
{
  public:
    void init() override
    {
        // TODO: One-time hardware initialisation
        // Example: configure GPIO pins, zero actuator outputs, etc.
    }

    void tick(SputterMicros /*systemTimeMicros*/) override
    {
        // TODO: Your periodic control logic
        // This runs every control cycle (default: 100 Hz).
        // Read sensors, update PID controllers, drive actuators, etc.
    }

    void handleCommand(const typename Cfg::Command & /*cmd*/) override
    {
        // TODO: Handle commands received from the CLI / command queue
        // Example:
        // switch (cmd.id) {
        //     case Cfg::CmdID::SET_GAS_FLOW:
        //         gasController.setSetpoint(cmd.value);
        //         break;
        //     case Cfg::CmdID::ABORT:
        //         transitionTo(Cfg::State::FAULT);
        //         break;
        // }
    }

    void forceSafeAbort() override
    {
        // TODO: Emergency shutdown — called when a safety monitor trips.
        // De-energise actuators, close valves, disable power supplies.
        // Must be fast and non-blocking.
    }
};

// =========================================================================
//  STEP 2: Implement Your Safety Monitor(s)
// =========================================================================

/**
 * @brief Hardware safety check — evaluated every tick before app logic.
 *
 * If isSafe() returns false, the kernel calls onSafeAbort() on your
 * application. Implement one monitor per independent safety concern
 * (pressure, arc detection, temperature, etc.).
 */
class MySafetyMonitor : public ISafetyMonitor
{
  public:
    bool isSafe() const override
    {
        // TODO: Check your hardware safety condition
        // Example: return pressureSensor.read() < kMaxPressure;
        return true; // Placeholder — always safe
    }

    const char *name() const override
    {
        return "MySafetyMonitor";
    }
};

// =========================================================================
//  STEP 3: Implement Your Stream (Serial / USB)
// =========================================================================

/**
 * @brief Bidirectional byte stream for CLI communication.
 *
 * Wraps your platform's serial port (UART, USB CDC, etc.).
 * All methods must be non-blocking.
 */
class MyStream : public IStream
{
  public:
    std::size_t available() const override
    {
        // TODO: Return number of bytes available to read
        return 0;
    }

    std::size_t read(uint8_t * /*buffer*/, std::size_t /*maxLen*/) override
    {
        // TODO: Non-blocking read from serial buffer
        return 0;
    }

    std::size_t write(const uint8_t * /*data*/, std::size_t /*len*/) override
    {
        // TODO: Write telemetry/response bytes to serial output
        return 0;
    }
};

// =========================================================================
//  STEP 4 (Optional): Implement Custom Tasks
// =========================================================================

// If you need additional periodic tasks beyond the built-in Control,
// Comms, and Diagnostics tasks, implement IScheduledTask:
//
// class MySensorTask : public IScheduledTask
// {
//   public:
//     uint32_t periodUs() const override { return 50000; } // 20 Hz
//     void init() override { /* sensor setup */ }
//     void tick(SputterMicros now) override { /* read sensor */ }
// };
//
// Then register it in main():  builder.core(0).addScheduledTask(&sensorTask);

// =========================================================================
//  STEP 5: Provide a Platform Clock
// =========================================================================

/**
 * @brief Platform-specific microsecond clock.
 *
 * Must return a monotonically increasing uint64_t count of microseconds.
 *
 * Platform examples:
 * - Arduino:  return micros();
 * - Pico SDK: return to_us_since_boot(get_absolute_time());
 * - STM32:    return HAL_GetTick() * 1000ULL;
 * - Host/Test: use std::chrono (shown below)
 */
static SputterMicros platformGetTimeMicros()
{
    // TODO: Replace with your platform's microsecond timer
    return 0;
}

// =========================================================================
//  main() — Wire Everything Together
// =========================================================================

int main()
{
    // -- Instantiate User Components ----------------------------------------
    MyApplication    app;
    MySafetyMonitor  safetyMonitor;
    MyStream         stream;

    // Multiple safety monitors can be registered:
    std::array<ISafetyMonitor *, 1> monitors = {&safetyMonitor};

    // -- Optional: Telemetry Logger -----------------------------------------
    // TelemetryLogger telemetry;

    // -- Build the Kernel ---------------------------------------------------
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    builder.setClockSource(platformGetTimeMicros);
    builder.setWatchdogKick(nullptr);  // Set to your platform's watchdog kick function

    // Register custom user tasks (if any):
    // builder.core(0).addScheduledTask(&mySensorTask);
    // builder.addBackgroundTask(&myLoggingTask);

    // Validate and finalise the system
    const BuildResult result = builder.build();
    if (!result)
    {
        // Build failed — result.error contains the reason
        return 1;
    }

    // -- Initialise the Kernel ----------------------------------------------
    System<Cfg>::init(0);

    // -- Main Loop ----------------------------------------------------------
    while (true)
    {
        const SputterMicros now = platformGetTimeMicros();

        // Tick all tasks on core 0 (control, comms, diagnostics, user tasks)
        System<Cfg>::tick(0, now);

        // Optional: drain telemetry to output
        // telemetry.drain(myWriteCallback);
    }

    return 0;
}

// =========================================================================
//  DUAL-CORE TEMPLATE (uncomment for kCoreCount = 2)
// =========================================================================
//
// For dual-core systems (e.g., RP2040), replace main() above with:
//
// #include <thread>  // Or your platform's core-launch mechanism
//
// int main()
// {
//     MyApplication    app;
//     MySafetyMonitor  safetyMonitor;
//     MyStream         stream;
//     std::array<ISafetyMonitor *, 1> monitors = {&safetyMonitor};
//
//     SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
//     builder.setStream(&stream);
//     builder.setClockSource(platformGetTimeMicros);
//     builder.setWatchdogKick(nullptr);
//
//     const BuildResult result = builder.build();
//     if (!result) { return 1; }
//
//     // -- Get sync handle for startup/shutdown barriers -------------------
//     auto &sync = System<Cfg>::multiCoreSync();
//
//     // -- Launch Core 1 ---------------------------------------------------
//     // On RP2040: multicore_launch_core1(core1_entry);
//     // On host (for testing): std::thread
//     //
//     // Core 1 entry function:
//     // void core1_entry() {
//     //     sync.setInit(1);
//     //     System<Cfg>::init(1);
//     //     sync.startupBarrier(1, 2000ms);
//     //
//     //     while (running) {
//     //         const auto now = platformGetTimeMicros();
//     //         System<Cfg>::watchdog().kick(1, now);
//     //         System<Cfg>::tick(1, now);
//     //     }
//     //
//     //     sync.shutdownBarrier(1, 2000ms);
//     // }
//
//     // -- Core 0 (main thread) --------------------------------------------
//     sync.setInit(0);
//     System<Cfg>::init(0);
//     sync.startupBarrier(0, 2000ms);  // Wait for both cores ready
//
//     while (true) {
//         const auto now = platformGetTimeMicros();
//         System<Cfg>::watchdog().kick(0, now);
//         System<Cfg>::tick(0, now);
//     }
//
//     // sync.shutdownBarrier(0, 2000ms);
//     return 0;
// }
