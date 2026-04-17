# SputterOS API Reference

**Version:** 1.0.0  
**Header root:** `include/sputteros/`  
**Single include:** `#include "sputteros/SputterOS.h"`

This document is the canonical reference for every public type, interface, and class in
SputterOS. Rationale and design context are in the companion docs (see §14).

---

## Table of Contents

1. [Single Include](#1-single-include)
2. [Core Types](#2-core-types)
3. [Version](#3-version)
4. [Configuration Contract](#4-configuration-contract)
5. [System Runtime — `System<Cfg>`](#5-system-runtime)
6. [Builder — `SystemBuilder<Cfg>` / `CoreBuilder<Cfg>`](#6-builder)
7. [User Application Interfaces](#7-user-application-interfaces)
8. [Task Interfaces](#8-task-interfaces)
9. [Safety](#9-safety)
10. [HAL](#10-hal)
11. [Synchronization Primitives](#11-synchronization-primitives)
12. [Observability](#12-observability)
13. [Utilities](#13-utilities)
14. [Cross-Reference to Design Docs](#14-cross-reference)

---

## 1. Single Include

```cpp
#include "sputteros/SputterOS.h"
```

`SputterOS.h` is the umbrella header. It transitively pulls in all component headers
listed below. In most user files this is the only SputterOS include needed.

| Component Header | Contents |
|---|---|
| `sputteros/ConfigTraits.h` | Configuration contract, fixed constants, optional-field extractors |
| `sputteros/OpResult.h` | `OpResult` status enum |
| `sputteros/Version.h` | Version macros and `Version` struct |
| `sputteros/Builder.h` | `SystemBuilder`, `CoreBuilder`, `BuildResult` |
| `sputteros/Kernel.h` | `System<Cfg>`, `KernelState`, `CoreDispatchMode` |
| `sputteros/OSAL.h` | Task interfaces, sync primitives, `SputterTime` |
| `sputteros/Interfaces.h` | `IProcessState` |
| `sputteros/HAL.h` | `ISputterDevice`, `IStream` |
| `sputteros/Logic.h` | `InterlockManager`, `IInterlockCondition`, `IFaultResponse` |
| `sputteros/Comms.h` | `CLI<Cfg>`, protocol framing types |
| `sputteros/Utils.h` | `PIDController`, `NonBlockingStopwatch`, `ErrorLogger`, `TelemetryLogger`, etc. |

---

## 2. Core Types

**Header:** `sputteros/osal/SputterTime.h`

### `SputterMicros`

```cpp
using SputterMicros = uint64_t;
```

64-bit monotonic timestamp in **microseconds**. Used for all kernel `tick()` calls,
task periods, WCET declarations, and deadline tracking. Wraps at ~584,942 years.

### `SputterMillis`

```cpp
using SputterMillis = uint32_t;
```

32-bit duration in **milliseconds**. Used for barrier timeouts and polling intervals.
Maximum representable duration: ~49.7 days.

### `MicrosecondSource`

```cpp
using MicrosecondSource = uint64_t (*)();
```

Platform clock function pointer. Injected via `SystemBuilder::setClockSource()`. Must
return a monotonically increasing count of microseconds. Called from any core.

**RP2350 example:**
```cpp
uint64_t myClockSource() { return to_us_since_boot(get_absolute_time()); }
```

---

### `OpResult`

**Header:** `sputteros/OpResult.h`

Lightweight status code for registration and mutation operations.

```cpp
enum class OpResult : uint8_t { OK = 0, FULL = 1, NULL_ARG = 2 };
bool succeeded(OpResult r);  // convenience — true when r == OK
```

| Value | Meaning |
|---|---|
| `OK` | Operation succeeded |
| `FULL` | Container at capacity |
| `NULL_ARG` | A required pointer argument was `nullptr` |

---

### `KernelState`

**Header:** `sputteros/kernel/KernelState.h`  
**Namespace:** `SputterOS::Kernel`

Top-level lifecycle state of the kernel, advanced by `build()`, `init()`, `run()`, and
safety abort signals.

| Value | Entered When |
|---|---|
| `UNCONFIGURED` | Before `build()` / after `reset()` |
| `CONFIGURED` | After `build()`, before `init()` |
| `INITIALIZING` | During `init()` — tasks being initialized |
| `RUNNING` | Normal operation — tick loop dispatching |
| `SUSPENDING` | Graceful pause requested |
| `SUSPENDED` | All tick loops paused |
| `ABORTING` | Safety abort in progress |
| `ABORTED` | Safe state reached — waiting for operator |
| `SHUTTING_DOWN` | Orderly shutdown — draining tasks |
| `SHUTDOWN` | Terminal state |

Query: `System<Cfg>::kernelState()`

---

### `CoreState`

**Header:** `sputteros/osal/sync/MultiCoreSync.h`  
**Namespace:** `SputterOS`

Per-core lifecycle state used by `MultiCoreSync<N>`.

| Value | Meaning |
|---|---|
| `UNBORN` | Core not yet started |
| `INIT` | Core performing local initialization |
| `READY` | Core ready to run |
| `ERROR` | Unrecoverable fault |
| `SHUTDOWN` | Orderly exit requested |

---

## 3. Version

**Header:** `sputteros/Version.h`

```cpp
// Preprocessor macros
#define SPUTTEROS_VERSION_MAJOR  1
#define SPUTTEROS_VERSION_MINOR  0
#define SPUTTEROS_VERSION_PATCH  0
#define SPUTTEROS_VERSION_INT    10000   // major*10000 + minor*100 + patch
#define SPUTTEROS_VERSION_STRING "1.0.0"

// Struct
namespace SputterOS {
  struct Version {
    static constexpr int         major  = SPUTTEROS_VERSION_MAJOR;
    static constexpr int         minor  = SPUTTEROS_VERSION_MINOR;
    static constexpr int         patch  = SPUTTEROS_VERSION_PATCH;
    static constexpr int         asInt  = SPUTTEROS_VERSION_INT;
    static constexpr const char* string = SPUTTEROS_VERSION_STRING;
  };
}
```

Use the macros for `#if` guards and `Version::string` / `Version::asInt` for runtime
logging and assertions.

---

## 4. Configuration Contract

**Header:** `sputteros/ConfigTraits.h`

Every SputterOS template class is parameterized on a user-supplied `Cfg` struct. The
struct must satisfy the contract below; `ConfigValidator<Cfg>` enforces it at compile
time via `static_assert`.

### Required Members

| Member | Kind | Description |
|---|---|---|
| `State` | `enum class` | Application state machine states |
| `CmdID` | `enum class : uint8_t` | Command identifier enumeration |
| `Command` | struct | IPC command packet |
| `Command::id` | `CmdID` | Command identifier field |
| `Command::targetDevice` | `uint8_t` | Target device index |
| `Command::value` | `float` | Command value payload |
| `kCoreCount` | `static constexpr std::size_t` | Number of cores (≥ 1) |
| `kQueueCapacity` | `static constexpr std::size_t` | Lock-free command queue capacity |

### Optional Members (with defaults)

| Member | Default | Description |
|---|---|---|
| `kMaxCommandsPerTick` | `8` | Max commands drained per control tick |
| `kMaxValidCommandID` | `255` | Highest valid `CmdID` value for range-check |
| `kControlBudgetUs` | `10000` | Control cycle period target in µs (100 Hz) |
| `kCommsBudgetUs` | `1000` | Max comms processing time per cycle in µs |
| `kDiagsBudgetUs` | `10000` | Max diagnostics processing time per cycle in µs |
| `kMaxBackgroundTasks` | `16` | Max registered background tasks |
| `kMaxFramePayload` | `256` | Max COBS frame payload in bytes |
| `kMaxLineLen` | `64` | Max TEXT-mode command line length in bytes |
| `kErrorLogCapacity` | `32` | `ErrorLogger` ring buffer capacity |
| `kTelemetryLogCapacity` | `32` | `TelemetryLogger` ring buffer capacity |
| `kMaxInterlockConditions` | `8` | Max `IInterlockCondition` registrations |
| `kMetricsWindowUs` | *(internal default)* | Rolling window for scheduler health metrics |
| `kCrunchMaxOverruns` | `10` | Consecutive CRUNCH WCET overruns before `onCrunchAbort()` |

### Minimal Config Example

```cpp
struct MyConfig {
    enum class State  { IDLE, PUMPING, FAULT };
    enum class CmdID : uint8_t { PUMP, ABORT };
    struct Command { CmdID id; uint8_t targetDevice; float value; };

    static constexpr std::size_t kCoreCount     = 2;
    static constexpr std::size_t kQueueCapacity = 16;
};
```

### Fixed Internal Constants

```cpp
static constexpr std::size_t kMaxTasksPerCore  = 256;  // not user-configurable
static constexpr std::size_t kMaxDevicesPerTask = 256; // not user-configurable
```

---

## 5. System Runtime

**Header:** `sputteros/kernel/System.h`  
**Template:** `System<Cfg>`

`System<Cfg>` is the zero-heap kernel runtime. All members are `static` — there is
exactly one kernel instance per `Cfg` type in the binary. Obtain components via its
static accessor methods after calling `build()`.

### Lifecycle Methods

| Signature | Description |
|---|---|
| `static void init(std::size_t coreId)` | Initialize all tasks on a core. Called once per core before `run()`. |
| `static void tick(std::size_t coreId, SputterMicros now)` | Advance all tasks on a core by one scheduling cycle. Used when the host controls the loop. |
| `static void run(std::size_t coreId)` | Blocking lifecycle entry point: init → startup barrier → tick loop → shutdown. Preferred over manual `tick()` calls. |
| `static void reset()` | Full teardown — reinitializes all state to `UNCONFIGURED`. Enables fault recovery and test re-use. |

### Guards & State Queries

| Signature | Description |
|---|---|
| `static bool isBuilt()` | Returns `true` if `build()` has completed without error. |
| `static Kernel::KernelState kernelState()` | Current lifecycle state. |
| `static std::size_t taskCount(std::size_t coreId)` | Number of scheduled tasks on a core. |
| `static ITask* task(std::size_t coreId, std::size_t idx)` | Scheduled task by index (0-based). |

### Safety Abort Bridge

| Signature | Thread Safety | Description |
|---|---|---|
| `static void signalSafetyAbort()` | Lock-free, any core | Sets the abort flag with `memory_order_release`. |
| `static bool isSafetyAborted()` | Lock-free, any core | Reads the abort flag with `memory_order_acquire`. |
| `static void clearSafetyAbort()` | Lock-free, any core | Clears the abort flag with `memory_order_relaxed`. |

### Component Accessors

All accessors return references to `inline static` instances owned by `System<Cfg>`.
Calls are safe after `build()` completes.

| Signature | Returns | Description |
|---|---|---|
| `static auto& commandQueue()` | `LockFreeQueue<Cfg, kQueueCapacity>` | SPSC cross-core command queue. Producer: CommsTask. Consumer: ControlTask. |
| `static auto& watchdog()` | `WatchdogSync<kCoreCount>` | Inter-core heartbeat monitor. |
| `static auto& multiCoreSync()` | `MultiCoreSync<kCoreCount>` | Startup / shutdown barrier. |
| `static SystemTimer& timer()` | `SystemTimer` | Kernel clock wrapping the injected `MicrosecondSource`. |
| `static ErrorLogger& errorLogger()` | `ErrorLogger` | ISR-safe fault / event ring buffer. |
| `static TelemetryLogger& telemetryLogger()` | `TelemetryLogger` | Human-readable task telemetry buffer. |
| `static MemoryProfiler& memProfiler()` | `MemoryProfiler` | Static memory usage tracker. |
| `static CoreUtilizationTracker& coreUtilization(std::size_t coreId)` | `CoreUtilizationTracker` | Per-core CPU utilization metrics. |
| `static QueueDepthMonitor& queueMonitor()` | `QueueDepthMonitor` | Rolling command queue depth statistics. |
| `static SchedulerHealthMetrics& schedulerHealth()` | `SchedulerHealthMetrics` | Aggregate scheduler health (miss rates, overruns). |
| `static PerformanceSnapshot snapshot()` | `PerformanceSnapshot` (by value) | Point-in-time copy of all metrics. ~1.2 KiB struct. |

### Background Task Accessors

| Signature | Description |
|---|---|
| `static IBackgroundTask* const* backgroundTasks()` | Pointer to the registered background task array. |
| `static std::size_t backgroundTaskCount()` | Number of registered background tasks. |

---

## 6. Builder

**Header:** `sputteros/builder/SystemBuilder.h`

### `BuildResult`

```cpp
struct BuildResult {
    bool        ok;     // true on success
    const char* error;  // null-terminated reason string on failure; nullptr on success
    explicit operator bool() const { return ok; }
};
```

### `SystemBuilder<Cfg>`

Fluent builder that wires all components together and transfers ownership to
`System<Cfg>`. Always call `build()` last; the result is `[[nodiscard]]`.

**Constructor:**

```cpp
SystemBuilder(IUserApplication<Cfg>* app,
              ISafetyMonitor**       monitors,
              std::size_t            monitorCount);
```

**Fluent configuration methods** — all return `SystemBuilder<Cfg>&` for chaining:

| Method | Description |
|---|---|
| `setStream(IStream*)` | Attach the serial/USB stream for CommsTask. |
| `setClockSource(MicrosecondSource)` | Inject the platform microsecond clock. |
| `setWatchdogKick(WatchdogKickFn)` | Register the hardware watchdog kick callback. |
| `addStopCondition(StopConditionFn)` | Register a `bool()` callback; returning `true` breaks the tick loop. |
| `setTelemetryDrain(DrainWriteFn, void* ctx)` | Register the write callback + opaque context for `TelemetryLogger::drain()`. |
| `setTelemetryMutex(IMutex*)` | Provide a mutex for multi-core `TelemetryLogger` access. |
| `addBackgroundTask(IBackgroundTask*)` | Register a background task into the Phase 2 ring. |
| `core(std::size_t coreId)` | Return a `CoreBuilder<Cfg>&` for per-core task registration. |

**Build:**

```cpp
[[nodiscard]] BuildResult build();
```

`build()` performs in order:

1. Validates required fields (`IUserApplication`, clock source, stream).
2. Applies optional-field defaults from `ConfigTraits`.
3. Emplaces `ScheduledControlTask` + `ScheduledCommsTask` into each core's task list.
4. Appends user tasks registered via `CoreBuilder::addScheduledTask()`.
5. Detects CRUNCH-mode cores; validates placement constraints.
6. **Sorts tasks by `effectivePriority()`** (insertion sort, stable for equal priorities).
7. Runs `validateDependencies()` on all tasks and registered devices.
8. Propagates clock source and metrics window to all components.
9. Advances `KernelState` to `CONFIGURED`.

Returns `BuildResult{false, "<reason>"}` on any validation failure; never throws.

---

### `CoreBuilder<Cfg>`

Obtained via `SystemBuilder<Cfg>::core(coreId)`.

| Method | Returns | Description |
|---|---|---|
| `addScheduledTask(IScheduledTask*)` | `CoreBuilder<Cfg>&` | Register a user scheduled task on this core. |
| `setCrunchTask(ICrunchTask*)` | `CoreBuilder<Cfg>&` | Set the exclusive CRUNCH task for this core. |
| `done()` | `SystemBuilder<Cfg>&` | Return to the parent builder (optional — method chaining works either way). |
| `taskCount()` | `std::size_t` | Number of user tasks registered so far. |
| `task(std::size_t idx)` | `IScheduledTask*` | User task by index. |
| `isActive()` | `bool` | `true` if at least one task has been registered. |

### Full Wiring Example

```cpp
MyApp app;
MyVacuumMonitor vacuumMon;
ISafetyMonitor* monitors[] = { &vacuumMon };

SputterOS::SystemBuilder<MyConfig> builder(&app, monitors, 1);
builder
    .setStream(&usbStream)
    .setClockSource(picoGetTimeMicros)
    .core(0).addScheduledTask(&motionTask).done()
    .addBackgroundTask(&diagTask);

auto result = builder.build();
if (!result) { panic(result.error); }

System<MyConfig>::run(0);   // Core 0 blocking entry point
```

---

## 7. User Application Interfaces

### `IUserApplication<Cfg>`

**Header:** `sputteros/kernel/interfaces/IUserApplication.h`  
**Template:** `IUserApplication<Cfg>` where `Cfg::Command` defines the command type.

The kernel's `ScheduledControlTask` owns one instance and calls these methods every
control tick. The user implements this interface to host their control logic.

| Method | Signature | Called When |
|---|---|---|
| `init` | `virtual void init() = 0` | `ControlTask::init()` — once, before the loop. |
| `tick` | `virtual void tick(SputterMicros now) = 0` | Every control cycle, after safety monitors pass. |
| `handleCommand` | `virtual void handleCommand(const Command& cmd) = 0` | For each command drained from the queue, before `tick()` in the same cycle. |
| `forceSafeAbort` | `virtual void forceSafeAbort() = 0` | When any `ISafetyMonitor::isSafe()` returns `false`. Must not block. |

Non-copyable. Use `IProcessState` (below) to decompose multi-phase logic.

---

### `IProcessState`

**Header:** `sputteros/interfaces/IProcessState.h`

Optional companion to `IUserApplication`. Implement one concrete `IProcessState` per
process phase (PumpDown, GasStabilize, Deposition, …) and delegate from
`IUserApplication::tick()`.

| Method | Signature | Description |
|---|---|---|
| `onEnter` | `virtual void onEnter() = 0` | Called once when entering this phase. |
| `execute` | `virtual void execute(SputterMicros now) = 0` | Called every tick while this phase is active. |
| `onExit` | `virtual void onExit() = 0` | Called once when leaving this phase. |

```cpp
// Delegation pattern:
void MyApp::tick(SputterMicros now) { m_currentState->execute(now); }
```

---

## 8. Task Interfaces

### `ITask` (base)

**Header:** `sputteros/osal/tasks/ITask.h`

All task interfaces inherit from `ITask`. Provides the two entry points the kernel calls.

| Method | Signature | Description |
|---|---|---|
| `init` | `virtual void init() = 0` | One-time initialization. |
| `tick` | `virtual void tick(SputterMicros now) = 0` | Advance by one scheduling cycle. |
| `isScheduled` | `virtual bool isScheduled() const` | Type marker — overridden by subclasses. |
| `isBackground` | `virtual bool isBackground() const` | Type marker — overridden by subclasses. |

---

### `IScheduledTask`

**Header:** `sputteros/osal/tasks/IScheduledTask.h`

Cooperative scheduled task running in Phase 1 of the tick loop. `periodUs()` is the
primary rate declaration; `schedulePriority()` determines dispatch order.

| Method | Signature | Default | Description |
|---|---|---|---|
| `periodUs` | `virtual SputterMicros periodUs() const = 0` | — | **Required.** Nominal execution period in µs. Used for RMS auto-priority and `kernelManagedPeriod` skip logic. |
| `declaredWcetUs` | `virtual SputterMicros declaredWcetUs() const` | `0` | Optional WCET declaration. Non-zero enables deadline miss detection and `onOverrun()` callback. |
| `schedulePriority` | `virtual uint8_t schedulePriority() const` | `0xFF` | Static dispatch priority. `0` = highest. `0xFF` = RMS auto (shorter period → higher priority). |
| `isIoPending` | `virtual bool isIoPending() const` | `false` | Return `true` to signal the kernel that I/O is in progress; the task will be skipped this tick. |
| `kernelManagedPeriod` | `virtual bool kernelManagedPeriod() const` | `false` | Opt-in: when `true`, the kernel skips dispatch if `periodUs()` has not elapsed since last call. |
| `onOverrun` | `virtual void onOverrun(SputterMicros actualUs, SputterMicros budgetUs)` | no-op | Callback invoked when `tick()` duration exceeds `declaredWcetUs()`. Use for task-level overrun recovery. |
| `isScheduled` | `bool isScheduled() const final` | `true` | Type marker. |
| `isBackground` | `bool isBackground() const final` | `false` | Type marker. |

**Priority Resolution** (performed at `build()` time by `SystemBuilder`):

1. If `schedulePriority() == 0xFF` (default), effective priority is derived from `periodUs()` — shorter period → numerically lower value → dispatched first (Rate-Monotonic Scheduling).
2. Explicit priorities (0–254) are always respected; equal priorities retain registration order (stable sort).
3. `ScheduledControlTask` uses priority `0`; `ScheduledCommsTask` uses priority `1`.

---

### `IBackgroundTask`

**Header:** `sputteros/osal/tasks/IBackgroundTask.h`

Best-effort task dispatched in Phase 2 (gap time). Never starves scheduled tasks.

| Method | Signature | Default | Description |
|---|---|---|---|
| `init` | `virtual void init() = 0` | — | Inherited from `ITask`. |
| `tick` | `virtual void tick(SputterMicros now) = 0` | — | Inherited from `ITask`. |
| `maxBudgetUs` | `virtual SputterMicros maxBudgetUs() const` | `1000` | Maximum execution budget per dispatch in µs. Kernel stops accounting at this limit. |
| `isBackground` | `bool isBackground() const final` | `true` | Type marker. |
| `isScheduled` | `bool isScheduled() const final` | `false` | Type marker. |

Register via `SystemBuilder::addBackgroundTask()`. `BackgroundDiagnosticsTask` is always
first in the ring and dispatches unconditionally.

---

### `ICrunchTask`

**Header:** `sputteros/osal/tasks/ICrunchTask.h`

Exclusive tight-loop task for CRUNCH-mode cores. Receives the full CPU of its core;
blocking calls (SPI, I2C) are permitted within `maxIterationUs()`.

| Method | Signature | Default | Description |
|---|---|---|---|
| `crunch` | `virtual void crunch(SputterMicros now) = 0` | — | **Required.** Tight-loop iteration. Called every `crunchPeriodUs()`. |
| `crunchPeriodUs` | `virtual SputterMicros crunchPeriodUs() const = 0` | — | **Required.** Nominal iteration period in µs. |
| `maxIterationUs` | `virtual SputterMicros maxIterationUs() const = 0` | — | **Required.** WCET per `crunch()` call. Overruns trigger `CRUNCH_OVERRUN` log and overrun counter. After `kCrunchMaxOverruns` consecutive overruns, `onCrunchAbort()` is called. |
| `onCrunchAbort` | `virtual void onCrunchAbort()` | no-op | Emergency shutdown callback after consecutive overruns exceed `kCrunchMaxOverruns`. |
| `isCrunchTask` | `bool isCrunchTask() const final` | `true` | Type marker. |

Register via `CoreBuilder::setCrunchTask()`. Only one CRUNCH task per core; exactly one
core may be in CRUNCH mode. The `crunch()` method replaces `tick()` — `tick()` is
never called by the kernel in CRUNCH mode.

---

## 9. Safety

### `ISafetyMonitor`

**Header:** `sputteros/kernel/interfaces/ISafetyMonitor.h`

Polled every tick by `ScheduledControlTask` before calling `IUserApplication::tick()`.
If any monitor returns `false`, `IUserApplication::forceSafeAbort()` is called.

| Method | Signature | Default | Description |
|---|---|---|---|
| `isSafe` | `virtual bool isSafe() const = 0` | — | **Required.** Return `true` = nominal, `false` = failsafe condition. Must complete in bounded time. |
| `name` | `virtual const char* name() const` | `"unnamed"` | Human-readable name for diagnostics output. |

Pass an array to `SystemBuilder`:
```cpp
ISafetyMonitor* monitors[] = { &vacuumMon, &coolantMon };
SystemBuilder<Cfg> builder(&app, monitors, 2);
```

---

### `InterlockManager`

**Header:** `sputteros/logic/InterlockManager.h`

User-space safety authority. Evaluates registered `IInterlockCondition` instances and
maintains hard/soft fault state. Must be evaluated by `IUserApplication` every tick.

```cpp
static constexpr std::size_t kMaxConditions = 8;
```

| Method | Signature | Description |
|---|---|---|
| `registerCondition` | `OpResult registerCondition(IInterlockCondition*)` | Register a condition. Returns `NULL_ARG` or `FULL` on failure. |
| `setFaultResponse` | `void setFaultResponse(IFaultResponse*)` | Set the hardware shutdown action called on hard fault. |
| `checkAllInterlocks` | `bool checkAllInterlocks() const` | Evaluate all conditions. `false` = at least one violated. Does not auto-trip. |
| `triggerSoftAbort` | `void triggerSoftAbort()` | Set soft-abort flag for the state machine to pick up. |
| `triggerHardFault` | `void triggerHardFault()` | Call `IFaultResponse::execute()` and latch the hard-fault flag. |
| `hasSoftAbort` | `bool hasSoftAbort() const` | `true` if soft abort is pending. |
| `isHardFaulted` | `bool isHardFaulted() const` | `true` if a hard fault is latched. |
| `clearFault` | `bool clearFault()` | Attempt to clear; returns `false` if conditions still violated. |
| `validateDependencies` | `bool validateDependencies() const` | Check that at least one condition and a fault response are set. |

---

## 10. HAL

### `ISputterDevice`

**Header:** `sputteros/hal/base/ISputterDevice.h`

Thin base for all SputterOS-managed hardware devices. `SystemBuilder` uses this
interface to automate initialization, validation, and health checks.

| Method | Signature | Default | ISR-safe | Description |
|---|---|---|---|---|
| `init` | `virtual bool init()` | `return true` | No | Hardware initialization — configure registers, buses, safe states. |
| `validate` | `virtual bool validate() const` | `return true` | No | Post-init validation — verify connectivity, self-test. |
| `isHealthy` | `virtual bool isHealthy() const = 0` | — | No | **Required.** Runtime health check. Called by the diagnostics loop. |
| `executeFastFault` | `virtual void executeFastFault()` | no-op | **Yes** | Minimum hardware-safe shutdown (e.g. disable RF). Must obey ISR constraints (no blocking, no heap, no mutex). |

**ISR Safety Contract for `executeFastFault()`:**  
- Write only to memory-mapped registers or `std::atomic<>`.  
- No blocking, sleeping, or yielding.  
- No mutex, semaphore, or RTOS lock.  
- No `new`, `delete`, or heap allocator.  
- No `printf`, `std::cout`, or blocking I/O.

---

### `IStream`

**Header:** `sputteros/hal/devices/IStream.h`

Bidirectional non-blocking byte-stream abstraction (USB CDC, UART, TCP). Used by
`ScheduledCommsTask` for all serial communication.

| Method | Signature | Description |
|---|---|---|
| `available` | `virtual std::size_t available() const = 0` | Bytes ready in receive buffer, without blocking. |
| `read` | `virtual std::size_t read(uint8_t* buf, std::size_t max_len) = 0` | Non-blocking read. Returns bytes actually read (0 if none). |
| `write` | `virtual std::size_t write(const uint8_t* data, std::size_t len) = 0` | Write for telemetry / responses. Returns bytes written. |
| `isConnected` | `virtual bool isConnected() const = 0` | `true` if the transport link is active. |

Implementations must be non-blocking — `read()` must return immediately.

---

## 11. Synchronization Primitives

### `MultiCoreSync<N>`

**Header:** `sputteros/osal/sync/MultiCoreSync.h`  
**Template:** `N_CORES` — compile-time core count (≥ 2).

Structured startup and shutdown barriers using `std::atomic` per-core state. No heap,
no virtual dispatch, safe on Cortex-M33.

**Construction:**
```cpp
explicit MultiCoreSync(ICoreErrorHandler* errorHandler = nullptr);
```

**State transition methods:**

| Method | Signature | Description |
|---|---|---|
| `setInit` | `void setInit(std::size_t coreId)` | Advance core to `INIT`. |
| `setReady` | `void setReady(std::size_t coreId)` | Advance core to `READY`. |
| `setError` | `void setError(std::size_t coreId, const char* reason)` | Enter `ERROR` and invoke error handler. |
| `setShutdown` | `void setShutdown(std::size_t coreId)` | Enter `SHUTDOWN`. |

**Barriers:**

| Method | Signature | Description |
|---|---|---|
| `startupBarrier` | `bool startupBarrier(std::size_t coreId, SputterMillis timeoutMs, SputterMillis pollIntervalMs = 1)` | Signal `READY`, then poll until all cores are `READY` or timeout. Returns `false` on timeout or any `ERROR`. |
| `shutdownBarrier` | `bool shutdownBarrier(std::size_t coreId, SputterMillis timeoutMs, SputterMillis pollIntervalMs = 1)` | Signal `SHUTDOWN`, then poll until all cores reach `SHUTDOWN` or timeout. |

**Query:**

| Method | Signature | Description |
|---|---|---|
| `state` | `CoreState state(std::size_t coreId) const` | Current `CoreState` of a core. |
| `anyError` | `bool anyError() const` | `true` if any core is in `ERROR`. |
| `allReady` | `bool allReady() const` | `true` if all cores are in `READY`. |

---

### `WatchdogSync<N>`

**Header:** `sputteros/osal/sync/WatchdogSync.h`  
**Template:** `N_CORES` — number of monitored cores/tasks (≥ 1).

Inter-core heartbeat monitor. Lock-free, safe from any core or ISR.

| Method | Signature | Description |
|---|---|---|
| `kick` | `void kick(std::size_t coreId, SputterMicros now)` | Update heartbeat timestamp for the given core. Call periodically from the owning core/task. |
| `isStale` | `bool isStale(std::size_t coreId, SputterMicros now, SputterMicros timeout) const` | `true` if the core has kicked at least once AND its last kick is older than `timeout`. Safe to poll from any core. |
| `lastKickTime` | `SputterMicros lastKickTime(std::size_t coreId) const` | Timestamp of the last `kick()` call, or `0` if never kicked. |
| `hasStarted` | `bool hasStarted(std::size_t coreId) const` | `true` if `kick()` has been called at least once. |
| `coreCount` | `static constexpr std::size_t coreCount()` | Compile-time `N_CORES`. |

Access the shared instance via `System<Cfg>::watchdog()`.

---

### `AtomicDoubleBuffer<T, CachePolicy>`

**Header:** `sputteros/osal/sync/AtomicDoubleBuffer.h`  
**Template:** `T` — value type (default-constructible, copyable). `CachePolicy` — defaults to `NoCachePolicy` (zero-cost no-op).

Wait-free SWSR double buffer for cross-core latest-value sharing. Use for sensor data,
control set-points, and state snapshots. Do **not** use for reliable command delivery —
use `LockFreeQueue` instead (intermediate values are silently discarded).

| Method | Signature | Thread Safety | Description |
|---|---|---|---|
| `write` | `void write(const T& value)` | Single producer | Publish a new value. Selects the inactive slot, copies, flushes cache, atomically swaps index. |
| `read` | `T read() const` | Single consumer | Returns the most recently written value (copy). Returns `T{}` if `write()` was never called. |

**Custom Cache Policy:**
```cpp
struct MyCachePolicy {
    static void flushBuffer(const void* addr, std::size_t bytes);
    static void invalidateBuffer(const void* addr, std::size_t bytes);
};
AtomicDoubleBuffer<SensorData, MyCachePolicy> buf;
```
Required on platforms with non-coherent D-cache (STM32H7, ESP32-S3 PSRAM).

---

### `LockFreeQueue<Cfg, Capacity>`

**Header:** `sputteros/osal/sync/LockFreeQueue.h`  
**Construction:** Via `System<Cfg>` — not directly instantiable.  
**Access:** `System<Cfg>::commandQueue()`

SPSC lock-free ring buffer. Producer: `ScheduledCommsTask` (Core 1 typically).
Consumer: `ScheduledControlTask` (Core 0).

| Method | Signature | Description |
|---|---|---|
| `try_push` | `bool try_push(const Command& cmd)` | Non-blocking enqueue. Returns `false` if full. |
| `push` | `bool push(const Command& cmd, SputterMillis)` | Interface-conformant push. `timeoutMs` ignored (always non-blocking). |
| `try_pop` | `bool try_pop(Command& cmd)` | Non-blocking dequeue. Returns `false` if empty. |
| `pop` | `bool pop(Command& cmd, SputterMillis)` | Interface-conformant pop. `timeoutMs` ignored. |
| `size` | `std::size_t size() const` | Approximate count (may be slightly stale). |
| `capacity` | `std::size_t capacity() const` | Usable capacity (= `Cfg::kQueueCapacity`). |
| `clear` | `void clear()` | Discard all items. Call only when quiescent. |

---

## 12. Observability

### `ErrorLogger`

**Header:** `sputteros/utils/logging/ErrorLogger.h`  
**Access:** `System<Cfg>::errorLogger()`

ISR-safe circular ring buffer for diagnostic fault events. Capacity: `32` entries.
When full, oldest entry is silently overwritten.

```cpp
static constexpr std::size_t kCapacity = 32;
```

**`ErrorCode` enum:**

| Code | Value | Meaning |
|---|---|---|
| `STATE_TRANSITION` | 0 | System state changed |
| `INTERLOCK_TRIP` | 1 | Interlock condition violated |
| `SOFT_ABORT` | 2 | Software abort triggered |
| `HARD_FAULT` | 3 | Latching hard fault triggered |
| `ARC_DETECTED` | 4 | Plasma arc event |
| `SENSOR_ERROR` | 5 | HAL sensor fault |
| `WATCHDOG_KICK` | 6 | Watchdog timer kicked |
| `TIMER_ROLLOVER` | 7 | System timer wrapped |
| `INVALID_STATE` | 8 | Invalid kernel state transition |
| `CRUNCH_OVERRUN` | 9 | CRUNCH task exceeded `maxIterationUs()` |
| `DEADLINE_MISS` | 10 | Scheduled task exceeded `declaredWcetUs()` |

**`Entry` struct:**
```cpp
struct Entry {
    ErrorCode     code;
    SputterMicros timestamp;  // µs
    float         value;      // optional numeric payload
};
```

**Methods:**

| Method | Signature | ISR-safe | Description |
|---|---|---|---|
| `log` | `void log(ErrorCode code, SputterMicros ts, float value = 0.0f)` | **Yes** | Enqueue an event. Overwrites oldest if full. |
| `read` | `bool read(Entry& entry)` | No | Dequeue oldest entry. Returns `false` if empty. |
| `count` | `std::size_t count() const` | No | Entries currently buffered. |
| `clear` | `void clear()` | No | Discard all entries. |

---

### `TelemetryLogger`

**Header:** `sputteros/utils/logging/TelemetryLogger.h`  
**Access:** `System<Cfg>::telemetryLogger()`

Human-readable ring buffer for live operator telemetry. Capacity: `32` entries.
Thread-safe when a mutex guard is provided at construction (or injected via `setMutex()`).

**Constants:**
```cpp
static constexpr std::size_t kTextLen  = 64;   // max message length incl. null
static constexpr std::size_t kCapacity = 32;   // ring buffer entries
```

**`Verbosity` enum:**

| Level | Value | Meaning |
|---|---|---|
| `CRITICAL` | 0 | System errors requiring immediate attention |
| `STATUS` | 1 | Normal operating status *(default threshold)* |
| `INFO` | 2 | Informational detail for debugging |
| `DEBUG` | 3 | Verbose developer tracing |

**`TaskID` enum:**

| Value | Meaning |
|---|---|
| `CONTROL` | Message from ControlTask |
| `COMMS` | Message from CommsTask |
| `DIAGNOSTICS` | Message from DiagnosticsTask |
| `SYSTEM` | Startup or cross-task system messages |

**Convenience aliases:** `TelemetryLoggerTaskID`, `TelemetryLoggerVerbosity`

**`Entry` struct:**
```cpp
struct Entry {
    SputterMicros timestamp;
    TaskID        task;
    Verbosity     level;
    char          text[kTextLen];
};
```

**Methods:**

| Method | Signature | Description |
|---|---|---|
| Constructor | `explicit TelemetryLogger(IMutex* guard = nullptr)` | Empty buffer, `STATUS` threshold. |
| `log` | `void log(TaskID task, const char* text, Verbosity level, SputterMicros timestamp)` | Buffer a message. Always stored regardless of threshold; filtering is at drain time. Overwrites oldest if full. |
| `drain` | `void drain(DrainWriteFn writeFn, void* ctx = nullptr)` | Write all buffered entries at or below the verbosity threshold via `writeFn`, then remove them. Format: `[<timestamp_ms>][<TaskName>] <text>\n`. |
| `setVerbosity` | `void setVerbosity(Verbosity level)` | Update the verbosity threshold. |
| `getVerbosity` | `Verbosity getVerbosity() const` | Current threshold. |
| `count` | `std::size_t count() const` | Entries currently buffered. |
| `clear` | `void clear()` | Discard all entries without draining. |
| `setMutex` | `void setMutex(IMutex* guard)` | Inject a mutex after construction. |

**`DrainWriteFn` type:**
```cpp
using DrainWriteFn = void (*)(const uint8_t* data, std::size_t len, void* ctx);
```

---

### `SystemTimer`

**Header:** `sputteros/osal/SputterTime.h`  
**Access:** `System<Cfg>::timer()`

Kernel clock wrapping the injected `MicrosecondSource`. Use `nowMicros()` on hot paths;
the `double`-returning helpers are for logging and configuration only.

| Method | Signature | Description |
|---|---|---|
| `setClockSource` | `void setClockSource(MicrosecondSource src)` | Inject or replace the platform clock. Captures epoch. |
| `hasClockSource` | `bool hasClockSource() const` | `true` if a non-null clock source has been injected. |
| `nowMicros` | `SputterMicros nowMicros() const` | Zero-based uptime in µs (raw minus epoch). Zero if no source. |
| `microseconds` | `double microseconds() const` | Current time as `double` µs. |
| `milliseconds` | `double milliseconds() const` | Current time as `double` ms. |
| `seconds` | `double seconds() const` | Current time as `double` s. |
| `toMicroseconds` | `static double toMicroseconds(SputterMicros us)` | Raw µs → `double` µs. |
| `toMilliseconds` | `static double toMilliseconds(SputterMicros us)` | Raw µs → `double` ms. |
| `toSeconds` | `static double toSeconds(SputterMicros us)` | Raw µs → `double` s. |

---

## 13. Utilities

### `PIDController`

**Header:** `sputteros/utils/PIDController.h`

Platform-agnostic discrete PID with anti-windup and output clamping. Timestamps are
caller-supplied so no hardware timer dependency exists.

**Constructor:**
```cpp
PIDController(float kp, float ki, float kd, float output_min, float output_max);
```

| Method | Signature | Description |
|---|---|---|
| `compute` | `float compute(float setpoint, float measured, SputterMicros timestamp)` | Compute clamped PID output. Returns `0` and records timestamp on first call after reset. |
| `setGains` | `void setGains(float kp, float ki, float kd)` | Update gains without reconstructing. |
| `setOutputLimits` | `void setOutputLimits(float min, float max)` | Update output clamp limits. |
| `getLastOutput` | `float getLastOutput() const` | Output from the most recent `compute()` call (0 before first call). |
| `reset` | `void reset()` | Clear integral accumulator, derivative history, and timestamp. Call when re-entering a control phase. |

---

### `NonBlockingStopwatch`

**Header:** `sputteros/utils/NonBlockingStopwatch.h`

Elapsed-time tracker that never blocks. Records a start timestamp; elapsed time is
computed by comparing against a caller-supplied current timestamp.

| Method | Signature | Description |
|---|---|---|
| `start` | `void start(SputterMicros now)` | Start or restart, recording `now` as the start time. |
| `stop` | `void stop()` | Stop without clearing the recorded start time. |
| `reset` | `void reset()` | Stop and clear the start time. |
| `elapsed` | `SputterMicros elapsed(SputterMicros now) const` | Time since `start()`. Returns `0` if stopped. |
| `hasExpired` | `bool hasExpired(SputterMicros now, SputterMicros duration) const` | `true` if running and `elapsed >= duration`. |
| `isRunning` | `bool isRunning() const` | `true` if `start()` was called more recently than `stop()`. |

---

## 14. Cross-Reference

| Topic | Primary Reference |
|---|---|
| Architecture overview and component diagram | [SystemArchitecture.md](SystemArchitecture.md) |
| Step-by-step bring-up guide | [ImplementationGuide.md](ImplementationGuide.md) |
| Scheduler algorithm, pseudocode, Mermaid diagrams | [SchedulingDesign.md](SchedulingDesign.md) |
| Multi-core setup, RP2350 worked example | [MultiCoreImplementation.md](MultiCoreImplementation.md) |
| Wire protocol, COBS framing, Python CLI | [CommsProtocol.md](CommsProtocol.md) |
| ISR safety rules and patterns | [ISRMethodology.md](ISRMethodology.md) |
| Test layout, running tests, adding new tests | [TestingGuide.md](TestingGuide.md) |
| Example project directory structure | [ExampleProjectStructure.md](ExampleProjectStructure.md) |
| Doxygen comment style guide | [CommentStyle.md](CommentStyle.md) |
| Roadmap and future plans | [futurePlans/LongTermPlans.md](futurePlans/LongTermPlans.md) |
