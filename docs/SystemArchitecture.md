# SputterOS System Architecture

This document details the architectural layers, functional components, and data flow of SputterOS.

---

## Architectural Overview

SputterOS separates concerns into five layers: **Builder**, **Kernel**, **Logic**, **OSAL**, and **HAL**. The user application sits above all layers and injects concrete implementations through pure abstract interfaces.

```mermaid
graph TB
    subgraph "Your Application"
        MAIN["main.cpp<br/>SystemBuilder wiring"]
        APP["IUserApplication&lt;Cfg&gt;<br/>process logic"]
        SAFETY["ISafetyMonitor[]<br/>safety adapters"]
        HALIMPL["HAL Drivers<br/>relay · stream"]
    end

    subgraph "Builder Layer"
        SB["SystemBuilder&lt;Cfg&gt;<br/>configuration + validation"]
        CB["CoreBuilder&lt;Cfg&gt;<br/>per-core topology"]
        BR["BuildResult<br/>[[nodiscard]] outcome"]
    end

    subgraph "System&lt;Cfg&gt; — Runtime Singleton"
        SYS["System&lt;Cfg&gt;<br/>inline static data members"]
        CT["ControlTask&lt;Cfg&gt;<br/>ICriticalTask"]
        CMT["CommsTask&lt;Cfg&gt;<br/>IAsyncTask"]
        DT["DiagnosticsTask<br/>IAsyncTask"]
        LFQ["LockFreeQueue&lt;Cfg,N&gt;<br/>SPSC ring buffer"]
        WD["WatchdogSync&lt;N&gt;<br/>heartbeat monitor"]
        MCS["MultiCoreSync&lt;N&gt;<br/>lifecycle barriers"]
        EL["ErrorLogger<br/>ISR-safe ring buffer"]
        MP["MemoryProfiler<br/>heap/stack tracking"]
    end

    subgraph "Logic Layer"
        IM["InterlockManager"]
        CP["CommandParser&lt;Cfg&gt;"]
        CLI["CLI&lt;Cfg&gt;"]
        PID["PIDController"]
    end

    subgraph "OSAL Interfaces"
        IT["ITask / ICriticalTask / IAsyncTask"]
        IMQ["IMessageQueue&lt;Cfg&gt;"]
        ICP["ICommandProducer / ICommandConsumer"]
    end

    subgraph "HAL Interfaces"
        ISR["IStreamReader"]
    end

    MAIN -->|"constructs"| SB
    SB -->|"build()"| SYS
    SB --> CB
    CB -->|"addTask()"| SYS
    SB --> BR

    APP -->|"injected into"| CT
    SAFETY -->|"evaluated by"| CT
    HALIMPL -->|"implements"| ISR

    SYS --> CT & CMT & DT & LFQ & WD & MCS & EL & MP
    CT -->|"try_pop()"| LFQ
    CMT -->|"try_push()"| LFQ
    DT -->|"reads timers"| CT & CMT
    DT --> EL & MP

    CT -->|"calls"| APP
    CMT -->|"uses"| CLI
    CLI --> CP
    CLI --> ISR
```

---

## Layer 1: Builder (`include/sputteros/builder/`)

The builder layer is the **configuration entry point**. It validates the system topology, creates the three kernel tasks, and populates the `System<Cfg>` runtime singleton.

### `SystemBuilder<Cfg>`

Configuration-only builder. Holds user-provided kernel dependencies (`IUserApplication`, `ISafetyMonitor[]`, `IStream`, `WatchdogKickFn`) and per-core `CoreBuilder` instances. After `build()`, the builder's job is done.

**Usage sequence:**
```
1. SystemBuilder<Cfg> builder(&app, monitors, count)   — construct
2. builder.setStream(&stream)                          — inject for CommsTask
3. builder.setWatchdogKick(kickFn)                     — inject for DiagnosticsTask
4. builder.core(0).addTask(&customTask)                — optional user tasks
5. auto result = builder.build()                       — validate + populate System
6. System<Cfg>::init(coreId)                           — initialise tasks
7. System<Cfg>::tick(coreId, now)                      — runtime loop
```

### `CoreBuilder<Cfg>`

Per-core topology helper returned by `builder.core(id)`. Wraps `System<Cfg>::CoreData` — task registrations write directly into the singleton's static storage.

### `BuildResult`

`[[nodiscard]]` outcome with `bool ok` and `const char* error`. Implicitly converts to `bool`.

### What `build()` Does

When `IUserApplication` is non-null:

1. Creates `ControlTask<Cfg>` via `KernelConstructTag` PassKey → emplaced into `System::s_controlTask`
2. Creates `CommsTask<Cfg>` → `System::s_commsTask`
3. Creates `DiagnosticsTask` → `System::s_diagsTask`
4. Prepends kernel tasks to their core's task lists — auto-assigned by type (safety-first tick order)
5. Validates: at least one core has tasks
6. Validates: core affinity (multi-core: no `IAsyncTask` on Core 0, no `ICriticalTask` on Core 1+)
7. Validates: task dependencies (`validateDependencies()` on every registered task)
8. Builds flat task list for `DiagnosticsTask::setMonitoredTasks()`
9. Sets `System::s_built = true`

### Enforcement Mechanisms

| Mechanism | What It Prevents |
|---|---|
| `LockFreeQueue()` private constructor | Direct queue construction bypassing `System` |
| `KernelConstructTag` PassKey | Direct construction of kernel tasks |
| `BuildResult` is `[[nodiscard]]` | Silently discarding `build()` result |
| `assert(s_built)` in `init()`/`tick()` | Runtime before topology validation |
| Core affinity check in `build()` | `ICriticalTask` on wrong core |
| `validateDependencies()` in `build()` | Missing device dependencies at startup |

---

## Layer 2: System — Runtime Singleton (`include/sputteros/kernel/System.h`)

`System<Cfg>` owns **all** shared kernel infrastructure as `inline static` data members. Each unique `Cfg` template argument gets its own independent set of statics — in practice one per binary.

### Static Data Members

| Member | Type | Purpose |
|---|---|---|
| `s_commandQueue` | `LockFreeQueue<Cfg, kQueueCapacity>` | SPSC inter-core command queue |
| `s_watchdog` | `WatchdogSync<kCoreCount>` | Per-core heartbeat monitor |
| `s_sync` | `MultiCoreSync<N>` or `NoOpMultiCoreSync` | Lifecycle barriers (conditional on `kMultiCore`) |
| `s_errorLogger` | `ErrorLogger` | ISR-safe 32-entry circular fault log |
| `s_memProfiler` | `MemoryProfiler` | Heap/stack high-water mark tracking |
| `s_timer` | `SystemTimer` | Kernel-owned wrapper around the injected microsecond source |
| `s_controlTask` | `std::optional<ControlTask<Cfg>>` | Emplaced by `build()` |
| `s_commsTask` | `std::optional<CommsTask<Cfg>>` | Emplaced by `build()` |
| `s_diagsTask` | `std::optional<DiagnosticsTask>` | Emplaced by `build()` |
| `s_cores[]` | `CoreData[kCoreCount]` | Per-core task lists |
| `s_allTasks[]` | `ITask*[kMaxTotalTasks]` | Flat task list for diagnostics |
| `s_allTaskCount` | `std::size_t` | Number of valid entries in `s_allTasks[]` |
| `s_built` | `bool` | Guard flag set by `build()` |
| `s_lastTime[]` | `SputterMicros[kCoreCount]` | Last observed tick time per core for rollover detection |

### Public Static API

| Method | Description |
|---|---|
| `init(coreId)` | Call `ITask::init()` on every task registered to this core |
| `tick(coreId, systemTime)` | Tick every task on this core; instruments with `TaskTimer::start()`/`stop()`. Detects timer rollover (logs `TIMER_ROLLOVER` fault). |
| `commandQueue()` | Access the lock-free command queue |
| `watchdog()` | Access the inter-core watchdog |
| `multiCoreSync()` | Access lifecycle barriers (or no-op stub) |
| `timer()` | Access the kernel-owned `SystemTimer` configured via `SystemBuilder::setClockSource()` |
| `errorLogger()` | Access the kernel-owned error logger |
| `memProfiler()` | Access the kernel-owned memory profiler |
| `isBuilt()` | Query whether `build()` has been called |
| `taskCount(coreId)` | Number of tasks registered on a core |
| `task(coreId, idx)` | Get a task pointer by core and index |

### Per-Core Task Storage (`CoreData`)

Lightweight struct with `ITask* tasks[kMaxTasks]` and `std::size_t taskCount`. Supports `addTask()` (append) and `prependTask()` (shift-right insert at front). `kMaxTasks` is fixed at 256 (full 8-bit range) — the user never needs to tune this value.

---

## Layer 3: Kernel Tasks (`include/sputteros/kernel/`)

Three internal `ITask` implementations in `namespace SputterOS::Kernel`. Constructors require `KernelConstructTag` — only `SystemBuilder<Cfg>` and `KernelTestAccess` can instantiate them.

### `ControlTask<Cfg>` — Deterministic Safety + Control (`ICriticalTask`)

Runs at a configurable fixed rate (default 100 Hz, set via `kControlBudgetUs`). Each `tick()`:

```mermaid
flowchart LR
    A["evaluateSafety()"] -->|all pass| B["processCommands()"]
    A -->|any fail| F["forceSafeAbort() + return"]
    B --> C["IUserApplication::tick()"]
```

1. **`evaluateSafety()`** — iterate all `ISafetyMonitor` instances. First `isSafe() == false` → `IUserApplication::forceSafeAbort()` + early return
2. **`processCommands()`** — drain up to `CfgMaxCommandsPerTick<Cfg>::value` commands via `ICommandConsumer::try_pop()` → `IUserApplication::handleCommand()`
3. **`IUserApplication::tick(systemTime)`**

**Dependencies:** `ICommandConsumer<Cfg>*` (the queue), `IUserApplication<Cfg>*`, `ISafetyMonitor**`, `size_t monitorCount`

### `CommsTask<Cfg>` — Serial Command Reception (`IAsyncTask`)

Runs asynchronously. Supports two modes: TEXT (legacy ASCII) and FRAMED (COBS binary). See [CommsProtocol.md](CommsProtocol.md) for full protocol specification.

**TEXT mode** — Each `tick()`:

1. `CLI<Cfg>::tick()` — drain up to 64 bytes from `IStreamReader`, feed each to `CommandParser<Cfg>`
2. Complete commands → `ICommandProducer::try_push()`. Success → `ACK <cmdId>\n`. Queue full → `NACK <cmdId> <targetDevice> <value>\n`

**FRAMED mode** — Each `tick()`:

1. `CLI<Cfg>::tick()` — feed bytes to `FrameDecoder`, dispatch via `ProtocolRouter` to `IProtocolHandler` callbacks
2. COMMAND frames → `ICommandProducer::try_push()`. Success → framed ACK. Queue full → framed NACK
3. HANDSHAKE_REQ → validates magic, sends HANDSHAKE_RESP
4. EXIT_HANDSHAKE → sends ACK, reverts to TEXT mode
5. METRICS_REQ → sends METRICS_RESP with performance snapshot

`ScheduledCommsTask<Cfg>` implements `IProtocolHandler<Cfg>` to handle all protocol callbacks.

**Dependencies:** `IStreamReader*`, `ICommandProducer<Cfg>*`

### `DiagnosticsTask` — System Health Monitor (`IAsyncTask`)

Non-templated. Each `tick()`:

1. Kick hardware watchdog via `WatchdogKickFn` (nullable to disable)
2. Scan all monitored `TaskTimer` instances against the configurable control budget (`kControlBudgetUs`, default 10 ms); log overruns to `ErrorLogger`
3. `MemoryProfiler::update()`
4. Every `kMemCheckInterval` (100) ticks: log memory health snapshot

**Dependencies:** `ErrorLogger&`, `MemoryProfiler&`, `WatchdogKickFn`

---

## Layer 4: HAL — Hardware Abstraction (`include/sputteros/hal/`)

Pure abstract C++ interfaces. All inherit `ISputterDevice` (non-copyable, protected ctor, virtual dtor, pure-virtual `isHealthy()`).

| Interface | Key Methods | Notes |
|---|---|---|

| `IStreamReader` | `available()`, `read()`, `write()`, `isConnected()` | Bidirectional byte stream. All non-blocking. Does not inherit `ISputterDevice`. |

### Dummy Stubs (`tests/unit/mocks/`)

1 no-op stub for immediate bring-up without real hardware:

| Stub | Behaviour |
|---|---|
| `DummyStreamReader` | Returns 0 bytes, discards writes |

---

## Layer 5: OSAL — OS Abstraction (`include/sputteros/osal/`)

The OSAL layer is organized into two subfolders:

- **`tasks/`** — Task abstraction hierarchy (`ITask`, `ICriticalTask`, `IAsyncTask`)
- **`sync/`** — Synchronization and queuing primitives (`IMessageQueue`, `LockFreeQueue`, `MultiCoreSync`, `WatchdogSync`, `IMutex`, etc.)
- **Root** — Shared types (`SputterTime.h` — 64-bit µs time type and `SystemTimer` class)

### Task Hierarchy

```mermaid
classDiagram
    class ITask {
        <<interface>>
        +init()
        +tick(SputterMicros systemTimeMicros)
        +isCritical() bool
        +isAsync() bool
        +timer() TaskTimer&
        +addDevice(ISputterDevice*) bool
        +device(idx) ISputterDevice*
        +deviceCount() size_t
        +validateDependencies() bool
    }
    class ICriticalTask {
        +isCritical() true
    }
    class IAsyncTask {
        +isAsync() true
    }
    class TaskTimer {
        +start()
        +stop()
        +lastDuration() microseconds
        +minDuration() microseconds
        +maxDuration() microseconds
        +getAverageDurationUs() float
        +sampleCount() uint32
        +overrunCount() uint32
        +deadlineMissCount() uint32
        +histogram() uint32[8]
        +percentileUs(p) microseconds
        +isOverBudget(budget) bool
        +reset()
    }

    ITask <|-- ICriticalTask
    ITask <|-- IAsyncTask
    ITask *-- TaskTimer : m_timer
```

- `ICriticalTask` — auto-assigned to Core 0 by `SystemBuilder::build()`.
- `IAsyncTask` — auto-assigned to Core 1 in multi-core (or Core 0 in single-core). Enforced at build time.
- `TaskTimer` — embedded in every `ITask`; `System::tick()` instruments `start()`/`stop()`.
- **Device Dependencies** — `ITask` tracks up to 16 registered `ISputterDevice*` pointers via `addDevice()`. Override `validateDependencies()` to declare required devices; `SystemBuilder::build()` calls this on every task and fails the build if any returns `false`.

### Message Queue Hierarchy

```mermaid
classDiagram
    class ICommandProducer~Cfg~ {
        <<interface>>
        +try_push(Command) bool
    }
    class ICommandConsumer~Cfg~ {
        <<interface>>
        +try_pop(Command&) bool
    }
    class IMessageQueue~Cfg~ {
        <<interface>>
        +push(Command, timeout) bool
        +pop(Command&, timeout) bool
        +size() size_t
        +capacity() size_t
        +clear()
    }
    class LockFreeQueue~Cfg_N~ {
        -m_buf[kSlots]
        -m_head atomic
        -m_tail atomic
        -LockFreeQueue() «private»
    }

    ICommandProducer <|-- IMessageQueue
    ICommandConsumer <|-- IMessageQueue
    IMessageQueue <|-- LockFreeQueue
```

- `LockFreeQueue<Cfg, N>` — SPSC ring buffer. Private constructor; only `System<Cfg>` (friend) can instantiate.
- `ICommandProducer` — write-only view used by `CommsTask`
- `ICommandConsumer` — read-only view used by `ControlTask`

### Synchronization Primitives

| Class | Purpose |
|---|---|
| `MultiCoreSync<N>` | Per-core lifecycle state machine: `UNBORN → INIT → READY → SHUTDOWN` (or `ERROR`). Startup/shutdown barriers with `std::chrono::milliseconds` timeouts. |
| `WatchdogSync<N>` | Atomic heartbeat per core. `kick(coreId, time)` updates timestamp; `isStale(coreId, time, timeout)` detects hangs. |
| `NoOpMultiCoreSync` | Stub for single-core configs (`kCoreCount < 2`). All methods are no-ops. |
| `SputterTime` | Type alias: `SputterMicros` (`uint64_t`), `MicrosecondSource` function pointer, `SystemTimer` class with `nowMicros()` (zero-based uptime from clock injection time), `milliseconds()`, `seconds()`. |

---

## Logic & Utility Components

### Logic Layer (`src/logic/`)

| Component | Description |
|---|---|
| `IUserApplication<Cfg>` | Primary kernel integration contract: `init()`, `tick()`, `handleCommand()`, `forceSafeAbort()` |
| `ISafetyMonitor` | Generic failsafe interface: `isSafe() → bool`, `name() → const char*` |
| `InterlockManager` | Registered `IInterlockCondition` evaluator + `IFaultResponse` executor. Hard/soft fault model. |
| `CommandParser<Cfg>` | ASCII byte-by-byte parser: `<CmdID> <targetDevice> <value>\n`. 64-byte buffer with auto-reset. |
| `IProcessState` | Per-phase interface: `onEnter()`, `execute(SputterMicros systemTimeMicros)`, `onExit()` |
| `IUserApplication<Cfg>` | User-space application injected into ControlTask. Provides `init()`, `tick()`, `handleCommand()`, `forceSafeAbort()`. |

### Utility Layer (`include/sputteros/utils/` and `src/utils/`)

The utility layer is organized into two subfolders:

- **`logging/`** — Logging and diagnostic components (`ErrorLogger`, `TelemetryLogger`, `LightweightStringBuilder`)
- **Root** — Standalone timing and control utilities (`MemoryProfiler`, `NonBlockingStopwatch`, `PIDController`)

| Component | Description |
|---|---|
| `CLI<Cfg>` | Stream + parser + string builder wrapper. `tick()` drains 64 bytes/call. |
| `ErrorLogger` | ISR-safe 32-entry circular ring buffer. 8 error codes (including `TIMER_ROLLOVER`). `std::chrono::milliseconds` timestamps. |
| `PIDController` | Discrete PID with anti-windup. `compute(setpoint, feedback, time)`. Call `reset()` on phase transitions. |
| `TelemetryLogger` | Task-tagged, verbosity-filtered live output. 32-entry buffer, drains to `IStreamReader`. |
| `MemoryProfiler` | Heap/stack high-water mark tracking. Platform stubs return 0 — subclass for real hardware. |
| `LightweightStringBuilder` | 128-byte fixed-capacity heap-free formatter. Chainable `append()`. |
| `NonBlockingStopwatch` | Monotonic timer: `hasExpired(time, duration) → bool`. |
| `TaskTimer` | Per-task execution timer with injectable clock. Tracks last/min/max/average duration, sample count, overrun count, deadline-miss count, and an 8-bucket duration histogram (512 µs per bucket). Embedded in every `ITask`. |
| `CoreUtilizationTracker` | Per-core windowed busy/total accumulator. Wall time is the dispatch window (tick start → tick end), excluding sleep between ticks. Auto-resets every 1000 ticks; exposes `getUtilization()` for the last complete window. |
| `SchedulerHealthMetrics` | Aggregate gap-time (idle time per tick), peak gap, total overruns, and total deadline misses across all tasks. Counters saturate at `UINT32_MAX` to prevent wrap-around. |
| `QueueDepthMonitor` | Command queue depth tracker: last/max/average depth. Sample count saturates at `UINT32_MAX`. |
| `PerformanceSnapshot` | POD value type (~1.2 KiB on stack). Captures a point-in-time copy of all metrics: per-core utilization, per-task histograms, queue depth, memory, and scheduler health. Returned by `System<Cfg>::snapshot()`. |
| `PerformanceFormatter` | Heap-free formatter consuming a `PerformanceSnapshot`. Writes key=value or CSV text into a caller-provided `char` buffer. Uses integer arithmetic for float formatting. |

---

## Configuration — Template ConfigTraits

SputterOS uses **compile-time template parameters**. Define a plain struct satisfying the `ConfigTraits` contract in `include/sputteros/ConfigTraits.h`:

| Member | Type | Required | Default |
|---|---|---|---|
| `State` | `enum class` | Yes | — |
| `CmdID` | `enum class : uint8_t` | Yes | — |
| `Command` | `struct { CmdID id; uint8_t targetDevice; float value; }` | Yes | — |
| `kCoreCount` | `static constexpr std::size_t` | Yes | — |
| `kQueueCapacity` | `static constexpr std::size_t` | Yes | — |
| `kMaxCommandsPerTick` | `static constexpr int` | Optional | 8 |
| `kMaxValidCommandID` | `static constexpr uint8_t` | Optional | 255 |
| `kControlBudgetUs` | `static constexpr uint32_t` | Optional | 10000 (10 ms / 100 Hz) |

`ConfigValidator<Cfg>` enforces `static_assert` checks at template instantiation.

---

## Data Flow — Single Tick

```mermaid
sequenceDiagram
    participant Main as main() loop
    participant Sys as System&lt;Cfg&gt;::tick()
    participant CT as ControlTask
    participant App as IUserApplication
    participant CMT as CommsTask
    participant CLI as CLI&lt;Cfg&gt;
    participant Q as LockFreeQueue
    participant DT as DiagnosticsTask

    Main->>Sys: tick(0, now)
    Sys->>CT: timer.start() → tick(now) → timer.stop()
    CT->>CT: evaluateSafety() — ISafetyMonitor[]
    CT->>Q: try_pop() × kMaxCommandsPerTick
    Q-->>CT: Command (or empty)
    CT->>App: handleCommand(cmd)
    CT->>App: tick(now)

    Main->>Sys: tick(1, now)
    Sys->>CMT: timer.start() → tick(now) → timer.stop()
    CMT->>CLI: tick() — drain bytes from IStreamReader
    CLI->>Q: try_push(cmd)
    Q-->>CLI: success/full
    CLI-->>CMT: ACK or NACK

    Sys->>DT: timer.start() → tick(now) → timer.stop()
    DT->>DT: kickWatchdog()
    DT->>DT: scan TaskTimers for budget violations
    DT->>DT: MemoryProfiler::update()
```

---

## Safety Architecture

### Three-Tier Safety Model

```mermaid
flowchart TB
    subgraph "Tier 0 — ISR Fast Fault (sub-μs)"
        ISR["Hardware ISR fires"] --> FF["executeFastFault()<br/>direct register write"]
        FF --> LATCH["atomic latch set"]
    end

    subgraph "Tier 1 — Kernel Safety Loop"
        LATCH --> SM["ISafetyMonitor::isSafe()"]
        SM -->|"false"| ABORT["IUserApplication::forceSafeAbort()"]
        SM -->|"true"| TICK["proceed to tick()"]
    end

    subgraph "Tier 2 — User-Space Interlocks"
        IM["InterlockManager"] --> COND["IInterlockCondition[]"]
        COND -->|"isSafe()==false"| HF["triggerHardFault()"]
        HF --> FR["IFaultResponse::execute()"]
    end
```

- **Tier 0 (ISR):** `executeFastFault()` in HAL drivers runs inside the ISR — sub-microsecond hardware disable
- **Tier 1 (Kernel):** `ControlTask::evaluateSafety()` iterates `ISafetyMonitor[]` every tick; first failure → `forceSafeAbort()`
- **Tier 2 (User-space):** `InterlockManager` evaluates registered `IInterlockCondition` objects. Wrap in `ISafetyMonitor` adapter for kernel evaluation.

### ISafetyMonitor vs InterlockManager Boundary

These two safety mechanisms serve different tiers and should not be confused:

| Aspect | `ISafetyMonitor` (Kernel Tier) | `InterlockManager` (User Tier) |
|---|---|---|
| **Layer** | Kernel (`ControlTask`) | Logic (user-space) |
| **Evaluation** | Every tick, before `processCommands()` | On-demand via `checkAllInterlocks()` |
| **Latency** | Sub-microsecond (must be non-blocking) | May involve complex multi-condition evaluation |
| **Failure action** | Immediate `forceSafeAbort()` — kernel-enforced | Soft abort / hard fault — user decides severity |
| **Interface** | `isSafe() → bool` | `IInterlockCondition::isSafe() → bool` |
| **Cardinality** | Small fixed array (passed to `SystemBuilder`) | Up to `kMaxConditions` registered dynamically |
| **Naming** | Unified: both use `isSafe()` returning `true` = nominal |

**Bridge pattern:** To evaluate `InterlockManager` conditions within the kernel safety loop, wrap it in an `ISafetyMonitor` adapter (see [ImplementationGuide.md](ImplementationGuide.md) for the `InterlockMonitor` adapter example).

---

## Core Affinity Rules

| Task Type | Core 0 | Core 1+ |
|---|---|---|
| `ICriticalTask` | Allowed | **Rejected** by `build()` |
| `IAsyncTask` | **Rejected** (multi-core) | Allowed |
| Plain `ITask` | Allowed | Allowed |

In single-core mode (`kCoreCount == 1`), all tasks run on Core 0 — affinity checks are skipped.

---

## Data Flow Summary

1. **Command reception**: The platform scheduler calls `CommsTask<Cfg>::tick(SputterMicros systemTimeMicros)` each cycle. `CLI<Cfg>` drains the `IStreamReader` byte stream, `CommandParser<Cfg>` assembles ASCII lines into `Cfg::Command` packets, and `CommsTask` pushes validated packets into the `LockFreeQueue<Cfg, N>` via `ICommandProducer::try_push()`. On success the host receives `ACK <cmdId>\n`; if the queue is full the host receives `NACK <cmd_id> <sub_id> <value>\n`.

2. **Safety evaluation**: `ControlTask<Cfg>::tick(SputterMicros systemTimeMicros)` calls `evaluateSafety()` first. It iterates all registered `ISafetyMonitor` instances and calls `isSafe()` on each. Any monitor returning `false` immediately calls `IUserApplication<Cfg>::forceSafeAbort()` and returns before the application ticks.

3. **Command dispatch**: `ControlTask<Cfg>` drains up to `CfgMaxCommandsPerTick<Cfg>::value` entries from the `LockFreeQueue` via `ICommandConsumer::try_pop()` and forwards each to `IUserApplication<Cfg>::handleCommand()`. The concrete implementation decides how to route each command - whether to trigger a phase transition, update a setpoint, or pass it through to the active process phase.

4. **Process execution**: `IUserApplication<Cfg>::tick(SputterMicros systemTimeMicros)` is delegated to the user-provided concrete implementation. A typical implementation advances the active `IProcessState::execute(SputterMicros systemTimeMicros)` phase, which reads sensors through HAL interfaces, feeds readings into `PIDController` and `NonBlockingStopwatch`, and drives actuator setpoints. When a transition condition is met the phase initiates the appropriate transition.

5. **Health monitoring**: `DiagnosticsTask::tick(SputterMicros systemTimeMicros)` kicks the hardware watchdog, checks `WatchdogSync::isStale()` for each core, scans all monitored per-task `TaskTimer` instances for budget violations, updates `MemoryProfiler` high-water marks, and periodically logs a memory snapshot to `ErrorLogger`.

6. **Telemetry response**: Phase implementations or tasks format responses using `LightweightStringBuilder`, deposit them into `CLI<Cfg>`'s builder, and call `CLI<Cfg>::flush()` to write back through `IStreamReader`.

---

## Dependency Injection and Startup Sequence

The recommended startup sequence in `main.cpp`:

```
1. Construct all concrete HAL objects   (relays, stream, and any domain-specific devices you define)
2. Implement and construct ISafetyMonitor adapters  (wrapping InterlockManager or domain checks)
3. Implement and construct your concrete IUserApplication<Cfg> and its IProcessState phase objects; wire device pointers into them
4. Construct SystemBuilder<Cfg> with IUserApplication and ISafetyMonitor[] pointers
5. Call builder.setStream(&stream) to enable CommsTask
6. Call builder.setWatchdogKick(kickFn) to enable DiagnosticsTask watchdog (optional)
7. Optionally add user tasks: builder.core(0).addTask(&customTask)
8. Call builder.build() — validates topology, creates kernel tasks, enforces core affinity
9. Call `System<Cfg>::init(coreId)` — initialises all tasks on that core
10. Enter the scheduler loop, calling `System<Cfg>::tick(coreId, SputterMicros(...))` each cycle
```

The kernel tasks (ControlTask, CommsTask, DiagnosticsTask) are created internally by `build()` — the user never sees their constructors. Safety monitors run first in the tick (ControlTask is prepended), then user tasks tick in registration order.

Failing to call `build()` before `init()` triggers an assertion. Discarding the `BuildResult` is a compiler warning (`[[nodiscard]]`).
