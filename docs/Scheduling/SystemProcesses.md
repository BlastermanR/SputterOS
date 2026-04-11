# System Process Architecture

Detailed design documentation for each of the core system processes in SputterOS, covering current architecture, responsibilities, design rationale, and the changes required to support AMP partitioned scheduling.

This document accompanies [AMPSchedulingDesign.md](AMPSchedulingDesign.md) and covers implementation-level detail for each process that the design doc references at a high level.

---

## Table of Contents

1. [ControlTask — Deterministic Safety & Control](#1-controltask--deterministic-safety--control)
2. [CommsTask — Serial I/O & Command Pipeline](#2-commstask--serial-io--command-pipeline)
3. [DiagnosticsTask — Health Monitoring & Observability](#3-diagnosticstask--health-monitoring--observability)
4. [System Singleton — Runtime Infrastructure Owner](#4-system-singleton--runtime-infrastructure-owner)
5. [SystemBuilder — Configuration & Validation](#5-systembuilder--configuration--validation)
6. [Cruncher — Per-Core Partitioned Scheduler](#6-cruncher--per-core-partitioned-scheduler)
7. [SystemScheduler — SMP Background Scheduler](#7-systemscheduler--smp-background-scheduler)

---

## 1. ControlTask — Deterministic Safety & Control

### 1.1 Layer & Core Affinity

| Property | Value |
|----------|-------|
| Architecture layer | Kernel |
| Current base class | `ICriticalTask` |
| Proposed base class | `IScheduledTask` (as `ScheduledControlTask<Cfg>`) |
| Core | **Core 0, Slot 0** (highest priority, always) |
| Current period | `kControlBudgetUs` (default 10,000 µs / 100 Hz) |
| Proposed period | Configurable via `kControlBudgetUs`, enforced by Cruncher |

### 1.2 Architecture

```
                    ┌─────────────────────────────────┐
                    │     ScheduledControlTask<Cfg>    │
                    │         (Core 0, Slot 0)         │
                    │                                   │
                    │  ┌─────────────────────────────┐  │
                    │  │   1. evaluateSafety()        │  │
                    │  │   ┌───────────────────────┐  │  │
                    │  │   │ ISafetyMonitor[0]      │  │  │
                    │  │   │ ISafetyMonitor[1]      │  │  │
                    │  │   │ ...                    │  │  │
                    │  │   └─── any fail? ──────────┘  │  │
                    │  │        │ yes → forceSafeAbort()│  │
                    │  │        │ no  ↓                 │  │
                    │  │   2. processCommands()        │  │
                    │  │   ┌───────────────────────┐  │  │
                    │  │   │ LockFreeQueue::try_pop │  │  │
                    │  │   │ drain all available    │  │  │
                    │  │   └───────────────────────┘  │  │
                    │  │        ↓                       │  │
                    │  │   3. app->tick(now)            │  │
                    │  │   ┌───────────────────────┐  │  │
                    │  │   │ IUserApplication<Cfg>  │  │  │
                    │  │   │ (user process logic)   │  │  │
                    │  │   └───────────────────────┘  │  │
                    │  └─────────────────────────────┘  │
                    └─────────────────────────────────┘
```

### 1.3 Current Duties

1. **Safety evaluation**: Iterates all registered `ISafetyMonitor*` instances every tick. If any monitor returns `!isSafe()`, triggers `forceSafeAbort()` on the user application. This is the **first** operation every tick — nothing else runs until safety is confirmed.

2. **Command drain**: Calls `try_pop()` on the `LockFreeQueue` consumer view (`ICommandConsumer<Cfg>*`) in a loop until the queue is empty. Each popped command is dispatched to the user application.

3. **Application tick**: Calls `IUserApplication<Cfg>::tick(now)` exactly once per cycle. The user application executes its domain logic (state machine transitions, PID updates, device commands).

### 1.4 Dependencies

| Dependency | Type | Injected By |
|------------|------|-------------|
| `ICommandConsumer<Cfg>*` | Consumer view of `LockFreeQueue` | `SystemBuilder` (PassKey constructor) |
| `IUserApplication<Cfg>*` | User-provided process logic | `SystemBuilder` (PassKey constructor) |
| `ISafetyMonitor**` | Array of safety monitors | `SystemBuilder` (PassKey constructor) |
| `TaskTimer` | Inherited from `ITask` | Self-owned |

### 1.5 Safety-Critical Properties

- **Runs first on Core 0**: The Cruncher guarantees Slot 0 / priority 0 dispatches before any other task.
- **Safety-before-everything**: `evaluateSafety()` is the first instruction in `tick()`. No user code, no command processing, no IO happens before safety is confirmed.
- **Abort propagation**: When `forceSafeAbort()` fires, the kernel transitions to `KernelState::ABORTING`. The Cruncher stops dispatching all non-safety slots.
- **Non-interruptible on Core 0**: Core 0 is ISR-free by convention (`kIsrContextBudgetUs[0] = 0`). ControlTask execution is never preempted by interrupts.

### 1.6 Changes Required

| Change | Detail |
|--------|--------|
| **Rebase to `IScheduledTask`** | Change from `ICriticalTask` (deleted) to `IScheduledTask`. Implement `periodUs()` returning `kControlBudgetUs`, `declaredWcetUs()` with measured value, `schedulePriority()` returning 0. |
| **Rename to `ScheduledControlTask<Cfg>`** | Reflects new hierarchy. Constructor still uses `KernelConstructTag` PassKey. |
| **IO_PENDING support** | ADC reads and sensor checks may benefit from non-blocking IO patterns. Add `isIoPending()` override if `evaluateSafety()` depends on a pending sensor read. However, **safety evaluation must never be deferred** — if a sensor is IO_PENDING, the safe choice is to treat it as a fault, not to skip evaluation. |
| **KernelState awareness** | During `ABORTING`, `tick()` runs a reduced path: safety evaluation + abort sequence only. No command drain, no app tick. |

---

## 2. CommsTask — Serial I/O & Command Pipeline

### 2.1 Layer & Core Affinity

| Property | Value |
|----------|-------|
| Architecture layer | Kernel |
| Current base class | `IAsyncTask` |
| Proposed base class | `IScheduledTask` (as `ScheduledCommsTask<Cfg>`) |
| Core | **Core 1, Slot 0** (highest priority on Core 1) |
| Current period | Best-effort (every tick) |
| Proposed period | `kCommsBudgetUs` (default 1,000 µs / 1 kHz) |

### 2.2 Architecture

```
                    ┌──────────────────────────────────┐
                    │      ScheduledCommsTask<Cfg>      │
                    │         (Core 1, Slot 0)          │
                    │                                    │
                    │  ┌──────────────────────────────┐  │
                    │  │   1. m_cli.tick()             │  │
                    │  │   ┌────────────────────────┐  │  │
                    │  │   │ IStream::available()    │  │  │
                    │  │   │ IStream::read()         │  │  │
                    │  │   │ FakeStreamReader parse  │  │  │
                    │  │   └────────────────────────┘  │  │
                    │  │        ↓                        │  │
                    │  │   2. Drain parsed commands      │  │
                    │  │   ┌────────────────────────┐  │  │
                    │  │   │ while (parser.hasCmd()) │  │  │
                    │  │   │   cmd = parser.next()   │  │  │
                    │  │   │   if queue.try_push()   │  │  │
                    │  │   │     → send ACK          │  │  │
                    │  │   │   else                  │  │  │
                    │  │   │     → send NACK         │  │  │
                    │  │   └────────────────────────┘  │  │
                    │  └──────────────────────────────┘  │
                    └──────────────────────────────────┘

        ┌────────────────────────────────────────────────────────┐
        │                  Data Flow                              │
        │                                                         │
        │  Serial RX → IStream → CLI → Parser → LockFreeQueue    │
        │                                          ↓              │
        │  (Core 1, producer)              (Core 0, consumer)     │
        │                                   ControlTask::          │
        │                                   processCommands()     │
        └────────────────────────────────────────────────────────┘
```

### 2.3 Current Duties

1. **Byte ingestion**: Calls `CLI<Cfg>::tick()` which reads all available bytes from the `IStream` and feeds them to the command parser.

2. **Command parsing**: The `CLI` uses `FakeStreamReader` to tokenize incoming bytes. Complete commands are queued internally in the parser.

3. **Queue push with back-pressure**: For each parsed command, attempts `try_push()` to the `LockFreeQueue` (producer view). On success, sends `ACK`. On failure (queue full), sends `NACK <cmd_id> <sub_id> <value>\n` so the host knows the command was rejected.

### 2.4 Dependencies

| Dependency | Type | Injected By |
|------------|------|-------------|
| `ICommandProducer<Cfg>*` | Producer view of `LockFreeQueue` | `SystemBuilder` (PassKey constructor) |
| `IStream*` | Platform serial interface | `SystemBuilder::setStream()` |
| `CLI<Cfg>` | Owned parser + formatter | Self-constructed in constructor |
| `TaskTimer` | Inherited from `ITask` | Self-owned |

### 2.5 IO Characteristics

CommsTask is the most IO-intensive kernel task:

- **Serial read** is non-blocking (`IStream::available()` returns 0 if no bytes), so `tick()` completes quickly when idle.
- **Serial write** (ACK/NACK) may block briefly if the transmit buffer is full. On most platforms this is < 10 µs for a short string.
- **IO_PENDING pattern**: CommsTask naturally fits the poll model — check if bytes are available, process them, return. No long-blocking IO. IO_PENDING is unlikely to be needed here since `IStream::available()` is instantaneous.

### 2.6 Changes Required

| Change | Detail |
|--------|--------|
| **Rebase to `IScheduledTask`** | Change from `IAsyncTask` (deleted) to `IScheduledTask`. Implement `periodUs()` returning `kCommsBudgetUs`, `schedulePriority()` returning 0 (highest on Core 1). |
| **Rename to `ScheduledCommsTask<Cfg>`** | Reflects new hierarchy. Constructor still uses `KernelConstructTag` PassKey. |
| **Explicit period** | Currently runs every tick (best-effort). With scheduling, runs at `kCommsBudgetUs` rate (default 1 kHz). This gives serial processing 1 ms cadence — sufficient for 115200 baud (~11.5 bytes/ms). Higher baud rates may need faster periods. |
| **ISR budget awareness** | Core 1 typically handles UART RX interrupts. The ISR latches bytes into a hardware FIFO; `IStream::read()` pulls from the FIFO. `kIsrContextBudgetUs[1]` must account for worst-case UART ISR time (~2-3 µs per byte batch). |

---

## 3. DiagnosticsTask — Health Monitoring & Observability

### 3.1 Layer & Core Affinity

| Property | Value |
|----------|-------|
| Architecture layer | Kernel |
| Current base class | `IAsyncTask` |
| Proposed base class | `IBackgroundTask` (as `BackgroundDiagnosticsTask`) |
| Core | Current: Core 1 (fixed). Proposed: **Any core** (SystemScheduler background) |
| Current period | Every tick (best-effort) |
| Proposed period | Time-boxed at `kDiagsBudgetUs` (default 10,000 µs / 10 ms max budget) |

### 3.2 Architecture

```
                    ┌────────────────────────────────────┐
                    │    BackgroundDiagnosticsTask        │
                    │    (SystemScheduler, any core)      │
                    │                                      │
                    │  ┌────────────────────────────────┐  │
                    │  │   1. kickWatchdog()             │  │
                    │  │   ┌──────────────────────────┐  │  │
                    │  │   │ WatchdogKickFn callback   │  │  │
                    │  │   │ (platform HW watchdog)    │  │  │
                    │  │   └──────────────────────────┘  │  │
                    │  │        ↓                          │  │
                    │  │   2. scanTaskTimers()             │  │
                    │  │   ┌──────────────────────────┐  │  │
                    │  │   │ For each monitored task:  │  │  │
                    │  │   │   if timer.max > budget:  │  │  │
                    │  │   │     log(SENSOR_ERROR)     │  │  │
                    │  │   └──────────────────────────┘  │  │
                    │  │        ↓                          │  │
                    │  │   3. updateMemoryProfiler()       │  │
                    │  │   ┌──────────────────────────┐  │  │
                    │  │   │ MemoryProfiler::update()  │  │  │
                    │  │   │ heap/stack high-water     │  │  │
                    │  │   └──────────────────────────┘  │  │
                    │  │        ↓                          │  │
                    │  │   4. periodicReport() [every 100] │  │
                    │  │   ┌──────────────────────────┐  │  │
                    │  │   │ log(WATCHDOG_KICK,        │  │  │
                    │  │   │     heapUsed + stackHigh) │  │  │
                    │  │   └──────────────────────────┘  │  │
                    │  └────────────────────────────────┘  │
                    └────────────────────────────────────┘
```

### 3.3 Current Duties

1. **Watchdog kick**: Calls the platform-specific `WatchdogKickFn` callback every tick. This prevents the hardware watchdog from resetting the MCU during normal operation.

2. **Task timer scan**: Iterates all monitored tasks (up to 16). For each, checks if `timer().maxDuration()` exceeds the control budget. If so, logs `SENSOR_ERROR` with the task's overrun amount.

3. **Memory profiling**: Updates `MemoryProfiler` with current heap usage and stack high-water mark.

4. **Periodic health report**: Every 100 ticks, logs a `WATCHDOG_KICK` entry to `ErrorLogger` containing the memory snapshot. This doubles as proof-of-life and memory trend data.

### 3.4 Dependencies

| Dependency | Type | Injected By |
|------------|------|-------------|
| `ErrorLogger&` | Kernel-owned diagnostic log | `SystemBuilder` (PassKey constructor, reference) |
| `MemoryProfiler&` | Kernel-owned memory tracker | `SystemBuilder` (PassKey constructor, reference) |
| `WatchdogKickFn` | Platform watchdog callback | `SystemBuilder::setWatchdogKick()` |
| `ITask*[]` | Array of monitored tasks (max 16) | `DiagnosticsTask::setMonitoredTasks()` |
| `uint32_t m_controlBudget` | Budget threshold for overrun detection | Constructor param (default 10,000 µs) |

### 3.5 Why Background (Not Scheduled)

DiagnosticsTask is a monitoring / housekeeping process, not a control process:

- **No hard deadline**: Missing a diagnostics tick doesn't compromise safety — safety is ControlTask's job.
- **Variable cost**: Memory profiling and timer scanning have variable duration depending on task count. Not ideal for a fixed-period Cruncher slot.
- **Best utilization**: Running in gap time means diagnostics execute when there's nothing more important to do, which is exactly the right priority.
- **Watchdog concern**: The one exception is watchdog kicking — if background tasks starve, the watchdog may fire. Mitigation: the SystemScheduler guarantees at least one background pass per Cruncher cycle if any gap exists, and the builder warns if utilization > 80% (meaning < 20% gap left for background).

### 3.6 Changes Required

| Change | Detail |
|--------|--------|
| **Rebase to `IBackgroundTask`** | Change from `IAsyncTask` (deleted) to `IBackgroundTask`. Implement `maxBudgetUs()` returning `kDiagsBudgetUs`. |
| **Rename to `BackgroundDiagnosticsTask`** | Reflects new hierarchy. Constructor still uses `KernelConstructTag` PassKey. |
| **Scheduler metrics** | Add `setMonitoredSlots(ScheduleSlot*, size_t)` to access Cruncher slot arrays. Report per-slot utilization (`wcet / period`), per-core utilization, ISR-adjusted utilization, deadline miss/overrun counters. |
| **Core-agnostic execution** | Remove assumption of Core 1 pinning. Background tasks may run on any core with gap time. Use `std::atomic` for any shared state accessed during monitoring. |
| **Watchdog strategy** | Evaluate whether watchdog kick should remain in DiagnosticsTask or move to a dedicated minimal scheduled task on each core (immune to background starvation). |

---

## 4. System Singleton — Runtime Infrastructure Owner

### 4.1 Layer & Role

| Property | Value |
|----------|-------|
| Architecture layer | Kernel |
| Template | `System<Cfg>` |
| Storage model | All `inline static` — zero heap, one instance per `Cfg` per binary |
| Primary role | Own all shared kernel infrastructure; provide `tick()` entry point per core |

### 4.2 Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     System<Cfg> Singleton                    │
│                                                              │
│  ┌───────────── Kernel Infrastructure ─────────────┐        │
│  │                                                   │        │
│  │  s_commandQueue     LockFreeQueue<Cfg, Cap>       │        │
│  │  s_watchdog         WatchdogSync<kCoreCount>      │        │
│  │  s_sync             MultiCoreSync<N> / NoOp       │        │
│  │  s_errorLogger      ErrorLogger (32 entries)      │        │
│  │  s_memProfiler      MemoryProfiler                │        │
│  │  s_timer            SystemTimer                   │        │
│  │                                                   │        │
│  ├───────────── Kernel Tasks ──────────────────────┤        │
│  │                                                   │        │
│  │  s_controlTask      optional<ControlTask<Cfg>>    │        │
│  │  s_commsTask        optional<CommsTask<Cfg>>      │        │
│  │  s_diagsTask        optional<DiagnosticsTask>     │        │
│  │                                                   │        │
│  ├───────────── Scheduling (NEW) ──────────────────┤        │
│  │                                                   │        │
│  │  s_kernelState      KernelState (internal FSM)    │        │
│  │  s_crunchers[]      Cruncher<kMaxSlots>[cores]    │        │
│  │  s_systemScheduler  SystemScheduler<kMaxBg>       │        │
│  │                                                   │        │
│  ├───────────── Per-Core Data ─────────────────────┤        │
│  │                                                   │        │
│  │  s_cores[]          CoreData[kCoreCount]          │        │
│  │  s_lastTime[]       SputterMicros[kCoreCount]     │        │
│  │  s_built            bool                          │        │
│  │                                                   │        │
│  └───────────────────────────────────────────────────┘        │
│                                                              │
│  ┌───────────── Static API ──────────────────────────┐      │
│  │                                                     │      │
│  │  init(coreId)        — Initialize tasks on core     │      │
│  │  tick(coreId, now)   — Dispatch via Cruncher        │      │
│  │  pause()             — KernelState → SUSPENDING     │      │
│  │  resume()            — KernelState → RUNNING        │      │
│  │  shutdown()          — KernelState → SHUTTING_DOWN  │      │
│  │  kernelState()       — Read-only state query        │      │
│  │  commandQueue()      — Accessor                     │      │
│  │  watchdog()          — Accessor                     │      │
│  │  errorLogger()       — Accessor                     │      │
│  │  memProfiler()       — Accessor                     │      │
│  │                                                     │      │
│  └─────────────────────────────────────────────────────┘      │
└─────────────────────────────────────────────────────────────┘
```

### 4.3 Current Duties

1. **Infrastructure ownership**: All shared kernel objects (`LockFreeQueue`, `WatchdogSync`, `MultiCoreSync`, `ErrorLogger`, `MemoryProfiler`, `SystemTimer`) are `inline static` members of `System<Cfg>`. No heap allocation.

2. **Task registration**: Per-core `CoreData` arrays hold `ITask*` pointers (max 256 per core). Tasks are registered by `SystemBuilder` during `build()`.

3. **Tick dispatch**: `tick(coreId, now)` iterates all tasks on the given core, wrapping each `tick()` with `timer().start()` / `timer().stop()`. Detects timer rollover.

4. **Init dispatch**: `init(coreId)` calls `init()` on all tasks registered to the given core.

5. **Accessor API**: Provides static getters for all infrastructure objects, used by kernel tasks and user code.

### 4.4 Changes Required

| Change | Detail |
|--------|--------|
| **Add `s_kernelState`** | `inline static KernelState s_kernelState{KernelState::UNCONFIGURED}`. Private setter (`transitionTo()`), public getter (`kernelState()`). Internal-only — see §10.1 of design doc. |
| **Add `s_crunchers[]`** | `inline static Cruncher<kMaxSlotsPerCore> s_crunchers[kCoreCount]`. One per core. |
| **Add `s_systemScheduler`** | `inline static SystemScheduler<kMaxBackgroundTasks> s_systemScheduler`. Shared across cores. |
| **Refactor `tick()`** | Replace flat task iteration with Cruncher delegation: `s_crunchers[coreId].tick(now)` → gap check → `s_systemScheduler.tick(now, gap)`. See §10.4 of design doc. |
| **Add lifecycle methods** | `pause()`, `resume()`, `shutdown()` — validate KernelState preconditions, trigger transitions. |
| **Rename kernel tasks** | `s_controlTask` becomes `optional<ScheduledControlTask<Cfg>>`, etc. |
| **Remove `CoreData::addTask(ITask*)`** | Replaced by Cruncher slot registration. `CoreData` may be simplified or removed. |

---

## 5. SystemBuilder — Configuration & Validation

### 5.1 Layer & Role

| Property | Value |
|----------|-------|
| Architecture layer | Builder |
| Template | `SystemBuilder<Cfg>` |
| Lifetime | Transient — created for `build()`, discarded after |
| Primary role | Validate user config, create kernel tasks, populate `System<Cfg>` |

### 5.2 Architecture

```
User code                          SystemBuilder<Cfg>
─────────                          ──────────────────
                                   ┌──────────────────────────────────┐
auto builder = SystemBuilder(app,  │  1. Store user app + monitors    │
               monitors, count);   │  2. Store platform dependencies  │
builder.setStream(&serial);        │     (stream, watchdog, clock)    │
builder.setWatchdogKick(kick);     │                                  │
builder.setClockSource(micros);    │  3. Core topology                │
                                   │  ┌────────────────────────────┐  │
builder.core(0)                    │  │ CoreBuilder[0]:            │  │
  .addScheduledTask(&pidTask)      │  │   Slot 0: ControlTask     │  │
  .done();                         │  │   Slot 1: pidTask          │  │
                                   │  │   (auto-sorted by period)  │  │
builder.core(1)                    │  ├────────────────────────────┤  │
  .addScheduledTask(&sensorTask)   │  │ CoreBuilder[1]:            │  │
  .done();                         │  │   Slot 0: CommsTask        │  │
                                   │  │   Slot 1: sensorTask       │  │
builder.addBackgroundTask(&logger);│  └────────────────────────────┘  │
                                   │                                  │
auto result = builder.build();     │  4. build():                     │
                                   │     a. Create kernel tasks       │
                                   │        (PassKey constructor)     │
                                   │     b. Register in Cruncher slots│
                                   │     c. Validate:                 │
                                   │        - Period floor checks     │
                                   │        - Utilization per core    │
                                   │        - ISR budget accounting   │
                                   │        - No duplicates           │
                                   │        - Dependencies            │
                                   │     d. Populate System<Cfg>      │
                                   │     e. Return BuildResult        │
                                   └──────────────────────────────────┘
```

### 5.3 Current Duties

1. **Kernel task creation**: Using `KernelConstructTag` PassKey, creates `ControlTask`, `CommsTask`, and `DiagnosticsTask` if a user application is provided.

2. **Core topology**: `CoreBuilder<Cfg>` instances (one per core) accept `addTask(ITask*)` and register tasks on the appropriate core's `CoreData`.

3. **Affinity validation**: Enforces `ICriticalTask` on Core 0 only, `IAsyncTask` on Core 1 only (in multi-core).

4. **Dependency validation**: Calls `validateDependencies()` on every registered task and on kernel tasks.

5. **Diagnostics wiring**: Calls `DiagnosticsTask::setMonitoredTasks()` with the flat list of all tasks.

6. **Build guard**: Sets `System<Cfg>::s_built = true` to enable `init()` and `tick()`.

### 5.4 Changes Required

| Change | Detail |
|--------|--------|
| **Replace `addTask(ITask*)`** | `CoreBuilder::addTask(ITask*)` is **removed**. Replaced by `CoreBuilder::addScheduledTask(IScheduledTask*)`. `SystemBuilder::addBackgroundTask(IBackgroundTask*)` for background tasks. |
| **Cruncher slot population** | In `build()`, after creating kernel tasks, populate each core's Cruncher slot table. Auto-assign priorities via RMS (shorter period = higher priority). Kernel tasks get Slot 0 on their respective cores. |
| **ISR-aware utilization check** | Compute `(kIsrContextBudgetUs[core] / minPeriod) + Σ(wcet[i] / period[i])` per core. Warn if > 0.8, fail if > 1.0. |
| **Period floor validation** | Assert `slot.periodUs >= kMinSchedulePeriodUs` for all slots. |
| **No duplicate check** | Same `IScheduledTask*` cannot appear in multiple slots or cores. |
| **Background ring population** | Register `BackgroundDiagnosticsTask` and user background tasks in `SystemScheduler` ring. |
| **KernelState transition** | `build()` transitions `s_kernelState` from `UNCONFIGURED` to `CONFIGURED`. |
| **Remove affinity markers** | No more `isCritical()` / `isAsync()` checks — core affinity is specified at registration time, not in the type system. |

---

## 6. Cruncher — Per-Core Partitioned Scheduler

### 6.1 Layer & Role

| Property | Value |
|----------|-------|
| Architecture layer | Kernel (new) |
| Template | `Cruncher<kMaxSlots>` |
| Storage model | `inline static` inside `System<Cfg>`, one per core |
| Primary role | Per-core deadline-driven periodic task dispatch |

### 6.2 Architecture

```
                 Cruncher<32>  (Core N)
                 ────────────────────────
                 ┌──────────────────────────────────────────────────┐
                 │  Slot Table (fixed array, sorted by priority)     │
                 │                                                    │
                 │  ┌────┬────────────┬────────┬──────┬───────────┐  │
                 │  │ #  │ Task       │ Period │ WCET │ State     │  │
                 │  ├────┼────────────┼────────┼──────┼───────────┤  │
                 │  │ 0  │ CtrlTask   │ 10ms   │ 2ms  │ IDLE      │  │
                 │  │ 1  │ PIDTask    │ 100µs  │ 30µs │ IO_PEND   │  │
                 │  │ 2  │ SensorPoll │ 1ms    │ 50µs │ READY     │  │
                 │  │ .. │ ...        │ ...    │ ...  │ ...       │  │
                 │  └────┴────────────┴────────┴──────┴───────────┘  │
                 │                                                    │
                 │  tick(now):                                        │
                 │    Phase 1: Scan slots → mark READY if due        │
                 │    Phase 2: Dispatch highest-priority READY        │
                 │    Phase 3: Return GAP_AVAILABLE if nothing due   │
                 │                                                    │
                 │  Per-Slot Tracking:                                │
                 │    DeadlineTracker (nextActivation, period, phase) │
                 │    TaskTimer ref (lastDuration, maxDuration, avg)  │
                 │    TaskState FSM (IDLE → READY → RUNNING → ...)   │
                 │    Overrun/deadline-miss counters                  │
                 └──────────────────────────────────────────────────┘
```

### 6.3 Duties

1. **Activation scan**: Every `tick()`, scan all enabled slots. Mark any slot with `nextActivation <= now` as `READY`.

2. **Priority dispatch**: Execute the highest-priority (lowest index) READY slot's task. Exactly one task dispatches per `tick()` call (non-preemptive).

3. **Timing instrumentation**: Wrap each `tick()` with `TaskTimer` start/stop. Update observed WCET. Check against declared WCET + ISR budget.

4. **Deadline track**: After execution, advance `nextActivation` by the slot's period. Skip missed periods (don't queue catch-up runs).

5. **Overrun detection**: If elapsed > period, mark OVERRUN and log. If elapsed > declaredWcet + isrBudget, log WCET_EXCEEDED.

6. **IO_PENDING tracking**: If the task signals IO pending after `tick()`, mark the slot as IO_PENDING instead of IDLE. Re-dispatch normally on next activation.

7. **Gap time reporting**: If no slots are READY, return `GAP_AVAILABLE` with time-to-next-deadline so `System::tick()` can yield to the SystemScheduler.

8. **KernelState respect**: During `ABORTING`, only dispatch Slot 0 (safety-critical). During `SUSPENDED`, dispatch nothing. During `SHUTDOWN`, dispatch nothing.

### 6.4 Design Decisions

| Decision | Rationale |
|----------|-----------|
| **Non-preemptive** | Simplicity, zero stack overhead, no context switch ASM, no shared-state corruption risk. See [PreemptionAnalysis.md](PreemptionAnalysis.md). |
| **RMS priority** | Shorter period = higher priority. Provably optimal for non-preemptive with utilization < Liu-Layland bound. |
| **One dispatch per tick()** | Avoids cascading dispatches in a single call. Each `System::tick()` invocation produces at most one task execution + one background task execution. Predictable call duration. |
| **Absolute deadlines** | Prevents drift accumulation. Missed periods are skipped, not queued. |
| **IO_PENDING not BLOCKED** | IO-heavy workloads need polled re-check, not event-driven wake-up. See §10.2 of design doc for full rationale. |

---

## 7. SystemScheduler — SMP Background Scheduler

### 7.1 Layer & Role

| Property | Value |
|----------|-------|
| Architecture layer | Kernel (new) |
| Template | `SystemScheduler<kMaxBg>` |
| Storage model | `inline static` inside `System<Cfg>`, system-wide |
| Primary role | Cooperative round-robin dispatch of background tasks in gap time |

### 7.2 Architecture

```
                 SystemScheduler<16>  (shared across cores)
                 ────────────────────────────────────────────
                 ┌────────────────────────────────────────────────────┐
                 │  Background Ring (fixed array, round-robin)         │
                 │                                                      │
                 │  ┌────┬──────────────┬────────┬──────────────────┐  │
                 │  │ #  │ Task         │ Budget │ Last Run         │  │
                 │  ├────┼──────────────┼────────┼──────────────────┤  │
                 │  │ 0  │ DiagTask     │ 10ms   │ 142,350,000 µs   │  │
                 │  │ 1  │ TeleDrain    │ 5ms    │ 142,345,000 µs   │  │
                 │  │ 2  │ UserLogger   │ 2ms    │ 142,340,000 µs   │  │
                 │  │ .. │ ...          │ ...    │ ...              │  │
                 │  └────┴──────────────┴────────┴──────────────────┘  │
                 │                                                      │
                 │  Per-Core State:                                     │
                 │    roundRobinIdx[coreId]  — independent index        │
                 │                                                      │
                 │  tick(now, gapBudgetUs):                             │
                 │    While remaining gap > kMinGapSliceUs:             │
                 │      Pick next enabled entry (round-robin)           │
                 │      If entry.maxBudgetUs <= remaining:              │
                 │        Acquire atomic running flag                   │
                 │        Execute task->tick(now)                       │
                 │        Release running flag                          │
                 │        Subtract elapsed from remaining               │
                 └────────────────────────────────────────────────────┘

                 ┌────────────────────────────────────────────────────┐
                 │  Cross-Core Safety                                  │
                 │                                                      │
                 │  entry.running : std::atomic<bool>                   │
                 │    Prevents same task from executing on both cores   │
                 │    simultaneously.                                   │
                 │                                                      │
                 │  Core 0 gap → tries DiagTask → acquires lock → runs │
                 │  Core 1 gap → tries DiagTask → lock held → skips    │
                 │                → tries TeleDrain → acquires → runs  │
                 └────────────────────────────────────────────────────┘
```

### 7.3 Duties

1. **Gap-time execution**: Only invoked by `System::tick()` when the Cruncher returns `GAP_AVAILABLE`. Receives the remaining gap budget (time until next Cruncher deadline).

2. **Round-robin fairness**: Each core maintains its own round-robin index. Tasks cycle fairly — no starvation within the background pool as long as gap time exists.

3. **Time-boxing**: Each background task declares a `maxBudgetUs`. The scheduler only dispatches a task if `maxBudgetUs <= remaining gap`. After execution, subtracts actual elapsed time from the gap budget.

4. **Mutual exclusion**: Per-entry `std::atomic<bool> running` flag prevents the same background task from running simultaneously on two cores. Lock-free via `exchange()` / `store()` with acquire/release semantics.

5. **KernelState respect**: Does not run during `SHUTDOWN`, `UNCONFIGURED`, or `ABORTING`. May run during `SUSPENDED` (background tasks are allowed while Cruncher is paused).

### 7.4 Design Decisions

| Decision | Rationale |
|----------|-----------|
| **Cooperative, not preemptive** | Background tasks are low-priority by definition. If one takes too long, the Cruncher will catch it on the next tick — the SystemScheduler just won't get gap time. No need for preemption infrastructure. |
| **Shared ring, per-core index** | Avoids duplicating the task array per core. Structure is read-only (populated at build time). Only the round-robin index and atomic flags are per-core / shared-atomic state. |
| **Time-boxed** | Prevents background tasks from starving real-time tasks. A background task that consistently overruns its budget is logged but not killed — it simply reduces gap time for others. |
| **Not core-pinned** | Background tasks aren't performance-critical. Letting them run on whichever core has idle time maximizes utilization without complicating the build API. |

### 7.5 Starvation Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| **High Cruncher utilization (> 95%)** | Builder warns at > 80%. At 100%, background tasks never run. User must reduce scheduled workload or accept no background work. |
| **Single large background task** | If one task's `maxBudgetUs` exceeds typical gap time, it never gets dispatched. **Mitigation:** Builder warns if any background task budget > average gap time. User can split into smaller work units. |
| **Watchdog starvation** | DiagnosticsTask kicks the watchdog. If it never runs, hardware watchdog fires. **Mitigation:** Monitor time-since-last-background-run. If > threshold, DiagnosticsTask reports itself as starved — but it can't report if it's not running. Consider a dedicated watchdog kick in the Cruncher's post-dispatch path as a safety net. |
