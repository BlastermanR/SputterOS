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
 * 5. Call `System<Cfg>::run(0)` — blocks until a stop condition fires
 *
 * ### Single-Core vs Dual-Core
 * This template defaults to single-core. For dual-core (e.g., RP2040):
 * - Set `kCoreCount = 2` in your config
 * - Implement an `IMutex` adapter for your platform (e.g. wrapping
 *   `std::timed_mutex` or FreeRTOS `xSemaphore`)
 * - See the DUAL-CORE section at the bottom of this file
 * - See exampleProjects/pingpong/ for a working dual-core example
 *
 * @author YourName
 * @date YYYY/MM/DD
 */

// ── Your Project Headers ────────────────────────────────────────────────────
#include "MyProjectConfig.h" // Your configuration struct

// ── SputterOS Headers ───────────────────────────────────────────────────────
// Option A: Include everything (convenient for getting started)
// #include "sputteros/SputterOS.h"

// Option B: Include only what you need (recommended for production)
#include "sputteros/Builder.h" // SystemBuilder
#include "sputteros/HAL.h"     // IStream
#include "sputteros/Kernel.h"  // System, ISafetyMonitor, IUserApplication
#include "sputteros/OSAL.h"    // SputterTime, tasks, sync
#include "sputteros/Utils.h"   // TelemetryLogger

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

    const char *name() const override { return "MySafetyMonitor"; }
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

/// @brief Drain callback — writes telemetry bytes to the output stream.
static void telemetryWrite(const uint8_t *data, std::size_t len, void *ctx)
{
    static_cast<IStream *>(ctx)->write(data, len);
}

int main()
{
    // -- Instantiate User Components ----------------------------------------
    MyApplication   app;
    MySafetyMonitor safetyMonitor;
    MyStream        stream;

    // Multiple safety monitors can be registered:
    std::array<ISafetyMonitor *, 1> monitors = {&safetyMonitor};

    // -- Build the Kernel ---------------------------------------------------
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    builder.setClockSource(platformGetTimeMicros);
    builder.setWatchdogKick(nullptr); // Set to your platform's watchdog kick function

    // Wire telemetry drain — BackgroundDiagnosticsTask calls this each tick.
    // User tasks log to System<Cfg>::telemetryLogger(); the kernel drains
    // automatically.
    builder.setTelemetryDrain(telemetryWrite, &stream);

    // Register stop conditions (OR'd — any one triggers run() exit):
    // builder.addStopCondition([]() -> bool { return /* your shutdown condition */; });

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

    // -- Run the Kernel (blocks until a stop condition fires) ---------------
    System<Cfg>::run(0);

    return 0;
}

// =========================================================================
//  DUAL-CORE TEMPLATE (for kCoreCount = 2)
// =========================================================================
//
// For dual-core systems (e.g., RP2040), replace main() above with the
// pattern below. You must provide a platform-specific IMutex implementation
// to guard the shared TelemetryLogger across cores.
//
// Example IMutex using std::timed_mutex (host builds only):
//
// #include "sputteros/osal/sync/IMutex.h"
// #include <mutex>
//
// class TimedMutexAdapter final : public SputterOS::IMutex
// {
//   public:
//     bool lock(std::chrono::milliseconds timeout) override
//     { return m_mtx.try_lock_for(timeout); }
//     bool try_lock() override { return m_mtx.try_lock(); }
//     void unlock() override { m_mtx.unlock(); }
//   private:
//     std::timed_mutex m_mtx;
// };
//
// For FreeRTOS, wrap xSemaphoreTake/xSemaphoreGive with pdMS_TO_TICKS.
// For Pico SDK, wrap mutex_enter_timeout_ms/mutex_exit.
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
//     // Mutex for shared TelemetryLogger across cores
//     TimedMutexAdapter telemetryMutex;
//
//     SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
//     builder.setStream(&stream);
//     builder.setClockSource(platformGetTimeMicros);
//     builder.setWatchdogKick(nullptr);
//     builder.setTelemetryDrain(telemetryWrite, &stream);
//     builder.setTelemetryMutex(&telemetryMutex);
//     builder.addStopCondition([]() -> bool { return /* shutdown */; });
//
//     // Register user tasks on cores
//     // builder.core(0).addScheduledTask(&core0Task);
//     // builder.core(1).addScheduledTask(&core1Task);
//
//     const BuildResult result = builder.build();
//     if (!result) { return 1; }
//
//     // Each core calls run() from its own thread / core-launch:
//     std::thread core1([]() { System<Cfg>::run(1); });
//     System<Cfg>::run(0);
//     core1.join();
//
//     return 0;
// }
