#ifndef MYPROJECT_CONFIG_H
#define MYPROJECT_CONFIG_H

/**
 * @file MyProjectConfig.h
 * @brief Default SputterOS configuration template — copy and customise.
 *
 * Every SputterOS application requires a configuration struct that satisfies
 * the `ConfigTraits` compile-time contract. This file documents every
 * user-configurable option with its purpose, constraints, and default values.
 *
 * ### Quick Start
 * 1. Copy this file into your project's `config/` directory.
 * 2. Rename the namespace and struct to match your project.
 * 3. Customise the State and CmdID enums for your process.
 * 4. Adjust constants for your hardware topology.
 *
 * @see include/sputteros/ConfigTraits.h for the compile-time validator.
 *
 * @author YourName
 * @date DD/MM/YYYY
 */

#include <cstddef>
#include <cstdint>

namespace MyProject
{

/**
 * @brief Configuration struct for the SputterOS kernel.
 *
 * Template parameter `Cfg` used by: `SystemBuilder<Cfg>`, `System<Cfg>`,
 * `IUserApplication<Cfg>`, `ScheduledControlTask<Cfg>`, `ScheduledCommsTask<Cfg>`,
 * `LockFreeQueue<Cfg, N>`, and `CommandParser<Cfg>`.
 */
struct MyProjectConfig
{
    // =====================================================================
    //  REQUIRED — State Machine
    // =====================================================================

    /**
     * @brief Master states of your process state machine.
     *
     * Define every top-level state your system can be in. The kernel itself
     * does not interpret these values — they are for your IUserApplication
     * implementation. Must be an `enum class`.
     *
     * Example states for a sputtering system:
     *   IDLE, PUMP_DOWN, GAS_STABILIZE, IGNITE, DEPOSITION, VENT, FAULT
     */
    enum class State : uint8_t
    {
        IDLE = 0,
        // TODO: Add your process states here
        // PUMP_DOWN,
        // GAS_STABILIZE,
        // DEPOSITION,
        // VENT,
        // FAULT,
    };

    // =====================================================================
    //  REQUIRED — Command Protocol
    // =====================================================================

    /**
     * @brief Command identifiers for inter-task communication.
     *
     * These are the operation codes sent over the serial CLI and routed
     * through the lock-free command queue to your application. Must be
     * `enum class : uint8_t`. Values 0–255.
     *
     * The CLI protocol is: `<CmdID> <targetDevice> <value>\n`
     * For example, sending "2 0 150.0\n" means CmdID=2, device=0, value=150.0
     */
    enum class CmdID : uint8_t
    {
        NOP = 0,
        // TODO: Add your command identifiers here
        // SET_STATE     = 1,
        // SET_GAS_FLOW  = 2,
        // SET_POWER     = 3,
        // SET_PRESSURE  = 4,
        // ABORT         = 5,
    };

    /**
     * @brief Standard command packet carried by the lock-free queue.
     *
     * This struct is the IPC message format between CommsTask (producer)
     * and ControlTask (consumer). All three fields are required.
     */
    struct Command
    {
        CmdID   id;           /**< @brief Which operation to perform. */
        uint8_t targetDevice; /**< @brief Target device index (0–255). */
        float   value;        /**< @brief Numeric payload (setpoint, rate, etc.). */
    };

    // =====================================================================
    //  REQUIRED — System Topology
    // =====================================================================

    /**
     * @brief Number of CPU cores used by the kernel.
     *
     * - `1` : Single-core — all tasks (control, comms, diagnostics) run on core 0.
     * - `2` : Dual-core AMP — ControlTask on core 0, CommsTask on core 1.
     *         Requires MultiCoreSync barriers in main().
     *
     * Must be >= 1. Typical values: 1 (Arduino, STM32) or 2 (RP2040 Pico).
     */
    static constexpr std::size_t kCoreCount = 1;

    /**
     * @brief Lock-free command queue capacity (number of command slots).
     *
     * SPSC ring buffer between CommsTask (producer) and ControlTask (consumer).
     * Commands that arrive when the queue is full are rejected (NACK).
     *
     * Sizing guidance:
     * - 8–16  : Typical for low-rate CLI interaction
     * - 32–64 : High-throughput scripted command streams
     *
     * Must be > 0. Power-of-two is not required but conventional.
     */
    static constexpr std::size_t kQueueCapacity = 16;

    // =====================================================================
    //  OPTIONAL — Command Processing
    // =====================================================================

    /**
     * @brief Maximum commands drained from the queue per ControlTask tick.
     *
     * Limits how many commands are processed in a single control cycle to
     * prevent command floods from starving safety evaluation. Remaining
     * commands stay queued for the next tick.
     *
     * Default: 8
     */
    // static constexpr int kMaxCommandsPerTick = 8;

    /**
     * @brief Highest valid raw command ID accepted by the CommandParser.
     *
     * Any CmdID with a raw value above this threshold is rejected as invalid.
     * Set to the raw value of your highest CmdID enum entry.
     *
     * Default: 255 (accept all)
     */
    // static constexpr uint8_t kMaxValidCommandID = 5;

    // =====================================================================
    //  OPTIONAL — Timing & Scheduling
    // =====================================================================

    /**
     * @brief Target control cycle period in microseconds.
     *
     * Determines the ControlTask activation rate. The reciprocal defines
     * the control frequency: 10000 µs → 100 Hz, 5000 µs → 200 Hz.
     *
     * Trade-offs:
     * - Lower (faster): tighter safety response, less headroom per tick.
     * - Higher (slower): more headroom for user logic, slower safety response.
     *
     * Default: 10000 (100 Hz)
     */
    // static constexpr uint32_t kControlBudgetUs = 10000;

    /**
     * @brief Maximum time budget for comms processing per scheduling cycle (µs).
     *
     * Caps how long the CommsTask spends parsing serial input per tick.
     *
     * Default: 1000 (1 ms)
     */
    // static constexpr uint64_t kCommsBudgetUs = 1000;

    /**
     * @brief Maximum time budget for diagnostics processing per cycle (µs).
     *
     * Caps how long the DiagnosticsTask spends on health checks per tick.
     *
     * Default: 10000 (10 ms)
     */
    // static constexpr uint64_t kDiagsBudgetUs = 10000;

    /**
     * @brief Minimum idle gap between scheduled task slices (µs).
     *
     * Ensures a small idle window between tasks for background work dispatch.
     *
     * Default: 10
     */
    // static constexpr uint64_t kMinGapSliceUs = 10;

    /**
     * @brief Minimum period between scheduler invocations (µs).
     *
     * Prevents the scheduler from spinning faster than hardware can support.
     *
     * Default: 10
     */
    // static constexpr uint64_t kMinSchedulePeriodUs = 10;

    /**
     * @brief Enable strict worst-case execution time monitoring.
     *
     * When true, the scheduler flags task overruns as errors. Useful during
     * development to catch tasks that exceed their timing budget.
     *
     * Default: false
     */
    // static constexpr bool kStrictWCET = false;

    // =====================================================================
    //  OPTIONAL — Capacity Limits
    // =====================================================================

    /**
     * @brief Maximum scheduling slots per core.
     *
     * Controls the partitioned AMP scheduler's internal slot table size.
     *
     * Default: 32
     */
    // static constexpr std::size_t kMaxSlotsPerCore = 32;

    /**
     * @brief Maximum number of background tasks that can be registered.
     *
     * Background tasks run in idle gaps with a budget cap.
     *
     * Default: 16
     */
    // static constexpr std::size_t kMaxBackgroundTasks = 16;

    /**
     * @brief ErrorLogger circular buffer capacity (entries).
     *
     * Larger values retain more diagnostic history at the cost of static RAM.
     *
     * Default: 32
     */
    // static constexpr std::size_t kErrorLogCapacity = 32;

    /**
     * @brief TelemetryLogger circular buffer capacity (entries).
     *
     * Default: 32
     */
    // static constexpr std::size_t kTelemetryLogCapacity = 32;

    /**
     * @brief Maximum number of IInterlockCondition instances per InterlockManager.
     *
     * Default: 8
     */
    // static constexpr std::size_t kMaxInterlockConditions = 8;

    /**
     * @brief Maximum command line length accepted by CommandParser (bytes).
     *
     * Default: 64
     */
    // static constexpr std::size_t kMaxLineLen = 64;

    // =====================================================================
    //  OPTIONAL — ISR Configuration
    // =====================================================================

    /**
     * @brief Per-core ISR context budget in microseconds.
     *
     * Reserves time for interrupt service routines. The scheduler deducts
     * this from the available budget before scheduling tasks.
     *
     * Array indexed by core ID: [core0_budget, core1_budget]
     *
     * Default: {0, 0} (no ISR reservation)
     */
    // static constexpr uint64_t kIsrContextBudgetUs[2] = {0, 0};
};

} // namespace MyProject

#endif // MYPROJECT_CONFIG_H
