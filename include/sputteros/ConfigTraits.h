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

namespace SputterOS
{

class ICriticalTask;
class IAsyncTask;

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
 * @brief Compile-time check: true if T is (or derives from) ICriticalTask.
 */
template <typename T> struct IsCriticalTask : std::is_base_of<ICriticalTask, T>
{
};

/**
 * @brief Compile-time check: true if T is (or derives from) IAsyncTask.
 */
template <typename T> struct IsAsyncTask : std::is_base_of<IAsyncTask, T>
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

} // namespace SputterOS

#endif // SPUTTEROS_CONFIG_TRAITS_H
