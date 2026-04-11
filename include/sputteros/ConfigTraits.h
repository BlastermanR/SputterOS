#ifndef SPUTTEROS_CONFIG_TRAITS_H
#define SPUTTEROS_CONFIG_TRAITS_H

/**
 * @file ConfigTraits.h
 * @brief Compile-time configuration contract for SputterOS template classes.
 *
 * The application supplies a single configuration struct as a template parameter
 * to the core classes (`ControlTask<Cfg>`, `CommsTask<Cfg>`, `IUserApplication<Cfg>`, etc.).
 *
 * This header documents the required interface that every user-defined
 * configuration struct must satisfy.
 *
 * ### Example
 *
 * @code
 * struct MyConfig {
 *     // -- Required nested types --
 *     enum class State   { IDLE, PUMPING, VENTING, FAULT_SAFE_MODE };
 *     enum class CmdID : uint8_t { SET_STATE, SET_GAS_FLOW, SET_POWER, ABORT };
 *
 *     struct Command {
 *         CmdID    id;
 *         uint8_t  targetDevice;
 *         float    value;
 *     };
 *
 *     // -- Required constants --
 *     static constexpr int   kMaxCommandsPerTick = 8;
 *     static constexpr CmdID kMaxValidCommandID  = CmdID::ABORT;
 *
 *     // -- System topology --
 *     static constexpr std::size_t kCoreCount     = 2;
 *     static constexpr std::size_t kQueueCapacity = 16;
 * };
 *
 * // Kernel tasks are created internally by SystemBuilder:
 * SputterOS::SystemBuilder<MyConfig> builder(&app, monitors.data(), monitors.size());
 * @endcode
 *
 * ### Required Members
 *
 * | Member                     | Kind              | Description                          |
 * |----------------------------|-------------------|--------------------------------------|
 * | `State`                    | enum class        | System state machine states          |
 * | `CmdID`                    | enum class : u8   | Command identifier enumeration       |
 * | `Command`                  | struct            | IPC command packet                   |
 * | `Command::id`              | CmdID             | Command identifier field             |
 * | `Command::targetDevice`    | uint8_t           | Target device index                  |
 * | `Command::value`           | float             | Command value payload                |
 * | `kMaxCommandsPerTick`      | static constexpr  | Max commands drained per tick        |
 * | `kMaxValidCommandID`       | static constexpr  | Highest valid CmdID for validation   |
 * | `kCoreCount`               | static constexpr  | Number of cores (>= 1)               |
 * | `kQueueCapacity`           | static constexpr  | Lock-free command queue capacity     |
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "sputteros/osal/SputterTime.h"

namespace SputterOS
{

class IScheduledTask;
class IBackgroundTask;

// =========================================================================
// Fixed capacity constants
// =========================================================================

/**
 * @brief Maximum tasks per core — fixed at 256 (full uint8_t range).
 *
 * This is not user-configurable. 256 slots per core is sufficient for
 * any realistic topology. The user never needs to tune this value.
 */
static constexpr std::size_t kMaxTasksPerCore = 256;

/**
 * @brief Maximum devices per task — fixed at 256 (full uint8_t range).
 *
 * This is not user-configurable. 256 devices per task is sufficient for
 * any realistic hardware set. The user never needs to tune this value.
 */
static constexpr std::size_t kMaxDevicesPerTask = 256;

// =========================================================================
// ConfigValidator
// =========================================================================

/**
 * @brief Compile-time validator for user-supplied configuration structs.
 *
 * Produces a clear static_assert message if any required member is missing.
 * Used internally by template classes; users need not call directly.
 *
 * @tparam Cfg User-supplied configuration struct.
 */
template <typename Cfg> struct ConfigValidator
{
    using Command = typename Cfg::Command;
    using CmdID   = typename Cfg::CmdID;

    static_assert(std::is_enum<typename Cfg::State>::value, "Cfg::State must be an enum class.");
    static_assert(std::is_enum<CmdID>::value, "Cfg::CmdID must be an enum class.");
    static_assert(sizeof(Command) > 0, "Cfg::Command must be a valid struct.");
    static_assert(Cfg::kCoreCount >= 1, "Cfg::kCoreCount must be >= 1.");
    static_assert(Cfg::kQueueCapacity > 0, "Cfg::kQueueCapacity must be > 0.");

    static constexpr bool value = true;
};

// =========================================================================
// Core-affinity type traits
// =========================================================================

/**
 * @brief Compile-time check: true if T is (or derives from) IScheduledTask.
 */
template <typename T> struct IsScheduledTask : std::is_base_of<IScheduledTask, T>
{
};

/**
 * @brief Compile-time check: true if T is (or derives from) IBackgroundTask.
 */
template <typename T> struct IsBackgroundTask : std::is_base_of<IBackgroundTask, T>
{
};

// =========================================================================
// Additional optional config field extraction with defaults
// =========================================================================

/**
 * @brief Extracts `Cfg::kMaxCommandsPerTick` or defaults to 8.
 */
template <typename Cfg, typename = void> struct CfgMaxCommandsPerTick
{
    static constexpr int value = 8;
};

template <typename Cfg> struct CfgMaxCommandsPerTick<Cfg, std::void_t<decltype(Cfg::kMaxCommandsPerTick)>>
{
    static constexpr int value = Cfg::kMaxCommandsPerTick;
};

/**
 * @brief Extracts `Cfg::kMaxValidCommandID` or defaults to 255.
 */
template <typename Cfg, typename = void> struct CfgMaxValidCommandID
{
    static constexpr uint8_t value = 255;
};

template <typename Cfg> struct CfgMaxValidCommandID<Cfg, std::void_t<decltype(Cfg::kMaxValidCommandID)>>
{
    static constexpr uint8_t value = Cfg::kMaxValidCommandID;
};

/**
 * @brief Extracts `Cfg::kControlBudgetUs` or defaults to 10000 (10 ms / 100 Hz).
 *
 * Determines the target control cycle period in microseconds. DiagnosticsTask
 * uses this to detect per-task budget overruns. The reciprocal defines the
 * target control rate (e.g. 10000 µs → 100 Hz, 5000 µs → 200 Hz).
 *
 * **Trade-offs:**
 * - Lower values (faster rate): tighter safety response latency, but less
 *   headroom per tick for user logic and ISafetyMonitor evaluation.
 * - Higher values (slower rate): more headroom per tick, but slower worst-case
 *   safety response and control loop update rate.
 */
template <typename Cfg, typename = void> struct CfgControlBudgetUs
{
    static constexpr uint32_t value = 10000;
};

template <typename Cfg> struct CfgControlBudgetUs<Cfg, std::void_t<decltype(Cfg::kControlBudgetUs)>>
{
    static constexpr uint32_t value = Cfg::kControlBudgetUs;
};

// =========================================================================
// Component capacity traits (optional overrides via Cfg)
// =========================================================================

/**
 * @brief Extracts `Cfg::kErrorLogCapacity` or defaults to 32.
 *
 * Controls the number of entries in the `ErrorLogger` circular ring buffer.
 * Larger values retain more diagnostic history at the cost of static RAM.
 *
 * @note ErrorLogger is not yet template-parameterized on this value.
 *       These traits are provided as standardized infrastructure for
 *       future parameterization without breaking existing code.
 */
template <typename Cfg, typename = void> struct CfgErrorLogCapacity
{
    static constexpr std::size_t value = 32;
};

template <typename Cfg> struct CfgErrorLogCapacity<Cfg, std::void_t<decltype(Cfg::kErrorLogCapacity)>>
{
    static constexpr std::size_t value = Cfg::kErrorLogCapacity;
};

/**
 * @brief Extracts `Cfg::kTelemetryLogCapacity` or defaults to 32.
 *
 * Controls the number of entries in the `TelemetryLogger` circular buffer.
 */
template <typename Cfg, typename = void> struct CfgTelemetryLogCapacity
{
    static constexpr std::size_t value = 32;
};

template <typename Cfg> struct CfgTelemetryLogCapacity<Cfg, std::void_t<decltype(Cfg::kTelemetryLogCapacity)>>
{
    static constexpr std::size_t value = Cfg::kTelemetryLogCapacity;
};

/**
 * @brief Extracts `Cfg::kMaxInterlockConditions` or defaults to 8.
 *
 * Controls the maximum number of `IInterlockCondition` instances
 * that can be registered with an `InterlockManager`.
 */
template <typename Cfg, typename = void> struct CfgMaxInterlockConditions
{
    static constexpr std::size_t value = 8;
};

template <typename Cfg> struct CfgMaxInterlockConditions<Cfg, std::void_t<decltype(Cfg::kMaxInterlockConditions)>>
{
    static constexpr std::size_t value = Cfg::kMaxInterlockConditions;
};

/**
 * @brief Extracts `Cfg::kMaxLineLen` or defaults to 64.
 *
 * Controls the maximum command line length accepted by `CommandParser`.
 */
template <typename Cfg, typename = void> struct CfgMaxLineLen
{
    static constexpr std::size_t value = 64;
};

template <typename Cfg> struct CfgMaxLineLen<Cfg, std::void_t<decltype(Cfg::kMaxLineLen)>>
{
    static constexpr std::size_t value = Cfg::kMaxLineLen;
};

// =========================================================================
// Scheduling config extractors (§10.7)
// =========================================================================

/**
 * @brief Extracts `Cfg::kMaxSlotsPerCore` or defaults to 32.
 *
 * Controls the maximum number of scheduling slots available per core
 * in the partitioned AMP scheduler.
 */
template <typename Cfg, typename = void> struct CfgMaxSlotsPerCore
{
    static constexpr std::size_t value = 32;
};

template <typename Cfg>
struct CfgMaxSlotsPerCore<Cfg, std::void_t<decltype(Cfg::kMaxSlotsPerCore)>>
{
    static constexpr std::size_t value = Cfg::kMaxSlotsPerCore;
};

/**
 * @brief Extracts `Cfg::kMaxBackgroundTasks` or defaults to 16.
 *
 * Controls the maximum number of background tasks that can be registered.
 */
template <typename Cfg, typename = void> struct CfgMaxBackgroundTasks
{
    static constexpr std::size_t value = 16;
};

template <typename Cfg>
struct CfgMaxBackgroundTasks<Cfg, std::void_t<decltype(Cfg::kMaxBackgroundTasks)>>
{
    static constexpr std::size_t value = Cfg::kMaxBackgroundTasks;
};

/**
 * @brief Extracts `Cfg::kCommsBudgetUs` or defaults to 1000 µs.
 *
 * Maximum time budget for comms processing per scheduling cycle.
 */
template <typename Cfg, typename = void> struct CfgCommsBudgetUs
{
    static constexpr SputterMicros value = 1000;
};

template <typename Cfg>
struct CfgCommsBudgetUs<Cfg, std::void_t<decltype(Cfg::kCommsBudgetUs)>>
{
    static constexpr SputterMicros value = Cfg::kCommsBudgetUs;
};

/**
 * @brief Extracts `Cfg::kDiagsBudgetUs` or defaults to 10000 µs.
 *
 * Maximum time budget for diagnostics processing per scheduling cycle.
 */
template <typename Cfg, typename = void> struct CfgDiagsBudgetUs
{
    static constexpr SputterMicros value = 10000;
};

template <typename Cfg>
struct CfgDiagsBudgetUs<Cfg, std::void_t<decltype(Cfg::kDiagsBudgetUs)>>
{
    static constexpr SputterMicros value = Cfg::kDiagsBudgetUs;
};

/**
 * @brief Extracts `Cfg::kMinGapSliceUs` or defaults to 10 µs.
 *
 * Minimum idle gap between scheduled task slices.
 */
template <typename Cfg, typename = void> struct CfgMinGapSliceUs
{
    static constexpr SputterMicros value = 10;
};

template <typename Cfg>
struct CfgMinGapSliceUs<Cfg, std::void_t<decltype(Cfg::kMinGapSliceUs)>>
{
    static constexpr SputterMicros value = Cfg::kMinGapSliceUs;
};

/**
 * @brief Extracts `Cfg::kMinSchedulePeriodUs` or defaults to 10 µs.
 *
 * Minimum period between scheduler invocations.
 */
template <typename Cfg, typename = void> struct CfgMinSchedulePeriodUs
{
    static constexpr SputterMicros value = 10;
};

template <typename Cfg>
struct CfgMinSchedulePeriodUs<Cfg, std::void_t<decltype(Cfg::kMinSchedulePeriodUs)>>
{
    static constexpr SputterMicros value = Cfg::kMinSchedulePeriodUs;
};

/**
 * @brief Extracts `Cfg::kStrictWCET` or defaults to false.
 *
 * When true, the scheduler enforces strict worst-case execution time
 * monitoring and will flag overruns as errors.
 */
template <typename Cfg, typename = void> struct CfgStrictWCET
{
    static constexpr bool value = false;
};

template <typename Cfg>
struct CfgStrictWCET<Cfg, std::void_t<decltype(Cfg::kStrictWCET)>>
{
    static constexpr bool value = Cfg::kStrictWCET;
};

/**
 * @brief Extracts `Cfg::kIsrContextBudgetUs` or defaults to {0, 0}.
 *
 * Per-core ISR context budget in microseconds. The specialization binds
 * directly to the user-supplied array, avoiding copies.
 */
template <typename Cfg, typename = void> struct CfgIsrContextBudgetUs
{
    static constexpr SputterMicros value[2] = {0, 0};
};

template <typename Cfg>
struct CfgIsrContextBudgetUs<Cfg, std::void_t<decltype(Cfg::kIsrContextBudgetUs[0])>>
{
    static constexpr auto& value = Cfg::kIsrContextBudgetUs;
};

} // namespace SputterOS

#endif // SPUTTEROS_CONFIG_TRAITS_H
