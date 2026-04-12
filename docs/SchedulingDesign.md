# SputterOS Scheduling Design

Documents the implemented scheduling model: task type hierarchy, dispatch algorithm, rate-limiting, IO_PENDING pattern, background tasks, and kernel state.

---

## Table of Contents

1. [Overview](#overview)
2. [Task Hierarchy](#task-hierarchy)
3. [Dispatch Algorithm](#dispatch-algorithm)
4. [Rate-Limiting Model](#rate-limiting-model)
5. [IO_PENDING Pattern](#io_pending-pattern)
6. [Background Tasks](#background-tasks)
7. [Kernel State Machine](#kernel-state-machine)
8. [Deadline Infrastructure](#deadline-infrastructure)
9. [Task Timer Instrumentation](#task-timer-instrumentation)
10. [Builder Registration API](#builder-registration-api)
11. [ConfigTraits Extensions](#configtraits-extensions)
12. [Testing Coverage](#testing-coverage)

---

## 1. Overview

SputterOS dispatches tasks through a **flat-loop cooperative scheduler** driven from the platform's main loop. Each call to `System<Cfg>::tick(coreId, now)` iterates all tasks registered on that core and calls `task->tick(now)` on each in declaration order. Timer instrumentation wraps every dispatch for post-hoc observability.

Scheduling behaviour — *which* task does work on a given tick — is **task-internal**. Each `IScheduledTask` subclass implements its own period check (`now - m_lastTick >= periodUs`) and returns early when the period has not elapsed. The kernel loop is oblivious to whether a task did real work or returned immediately; it always calls `tick()` and records the duration.

This model gives:

- **Deterministic loop time** — no dynamic dispatch decisions, no sorting, no priority queuing at the kernel level.
- **Zero heap** — all state lives in `inline static` members of `System<Cfg>`.
- **Task autonomy** — individual tasks declare their own periods and manage their own rate-limiting, keeping the kernel loop simple and auditable.
- **Observability** — `TaskTimer` records execution time for every task on every tick regardless of whether the task did work.

---

## 2. Task Hierarchy

```
ITask  (base — lifecycle + device deps + timer)
├── IScheduledTask  (periodic, rate-limited by task, has period + WCET + IO_PENDING)
└── IBackgroundTask (best-effort, budget-capped)
```

`ITask` is the root interface — never registered directly. All user tasks and kernel tasks subclass either `IScheduledTask` or `IBackgroundTask`.

### 2.1 IScheduledTask

Defined in `include/sputteros/osal/tasks/IScheduledTask.h`.

```cpp
class IScheduledTask : public ITask {
public:
    virtual SputterMicros periodUs() const = 0;        // required
    virtual SputterMicros declaredWcetUs() const { return 0; }   // 0 = auto-profile
    virtual uint8_t       schedulePriority() const { return 0xFF; } // 0xFF = RMS auto
    virtual bool          isIoPending() const { return false; }
    bool isScheduled()  const final { return true; }
    bool isBackground() const final { return false; }
};
```

Subclasses must implement `periodUs()` and the `ITask` interface (`init()`, `tick()`). The task is responsible for enforcing its own period inside `tick()`.

| Method | Purpose |
|--------|---------|
| `periodUs()` | Task's nominal activation interval in µs. Used by `SystemBuilder` for period-floor validation. Documented to calling code and diagnostics. |
| `declaredWcetUs()` | User-declared worst-case execution time. Used by `BackgroundDiagnosticsTask` overrun reporting. Default 0 = auto-profile from observed `TaskTimer` data. |
| `schedulePriority()` | Static priority hint (lower = higher priority). Unused by current flat-loop scheduler; reserved for future Cruncher integration. |
| `isIoPending()` | Task signals it yielded early for async I/O. Polled by some example projects; not yet gated by `System::tick()`. |
| `onSuspend()` / `onResume()` | Optional lifecycle callbacks. Called if the kernel suspends / resumes task execution. Currently no-op. |

### 2.2 IBackgroundTask

Defined in `include/sputteros/osal/tasks/IBackgroundTask.h`.

```cpp
class IBackgroundTask : public ITask {
public:
    virtual SputterMicros maxBudgetUs() const { return 1000; }
    bool isBackground() const final { return true; }
    bool isScheduled()  const final { return false; }
};
```

Background tasks declare a maximum per-tick execution budget via `maxBudgetUs()`. The budget is recorded by `TaskTimer` and reported by `BackgroundDiagnosticsTask`; the kernel does not currently preempt tasks that exceed it.

### 2.3 Kernel Task Implementations

| Task | Base | Core | Role |
|------|------|------|------|
| `ScheduledControlTask<Cfg>` | `IScheduledTask` | 0 (slot 0) | Safety eval → command drain → user app tick |
| `ScheduledCommsTask<Cfg>` | `IScheduledTask` | 1 (slot 0, or 0 in single-core) | Serial ingestion → CLI parse → queue push |
| `BackgroundDiagnosticsTask` | `IBackgroundTask` | Shared core list (Phase 3: background ring) | Watchdog kick, timer scan, memory profile |

`ICriticalTask` and `IAsyncTask` are retired. Core affinity is specified at registration time via `builder.core(N).addScheduledTask()`.

---

## 3. Dispatch Algorithm

`System<Cfg>::tick()` is the kernel's inner loop entry point:

```cpp
static void tick(std::size_t coreId, SputterMicros now)
{
    // 1. Guard: build must have succeeded
    assert(s_built);
    if (coreId >= kCoreCount) return;

    // 2. Rollover detection
    if (now < s_lastTime[coreId] && s_lastTime[coreId] != 0)
        s_errorLogger.log(ErrorCode::TIMER_ROLLOVER, now, 0.0f);
    s_lastTime[coreId] = now;

    // 3. Flat task loop — all tasks on this core, in declaration order
    for (std::size_t t = 0; t < s_cores[coreId].taskCount; ++t)
    {
        ITask *tsk = s_cores[coreId].tasks[t];
        if (tsk)
        {
            tsk->timer().start();
            tsk->tick(now);
            tsk->timer().stop();
        }
    }
}
```

**Key properties:**

- Tasks execute in **registration order**. Kernel tasks are prepended at build time, so the order is always: `ScheduledControlTask` → `ScheduledCommsTask` + `BackgroundDiagnosticsTask` → user tasks.
- Every task is called **every tick** regardless of whether its period has elapsed. Tasks that aren't due return in a few nanoseconds.
- Timer instrumentation records real wall-clock duration even for early-return ticks, providing accurate idle overhead measurements.
- `System::tick()` **does not** check `isIoPending()` or enforce `maxBudgetUs()` — these are enforced by the task and reported to diagnostics.

### 3.1 Tick Loop Sequencing (Mermaid)

```mermaid
sequenceDiagram
    participant Main as main() loop
    participant Sys as System&lt;Cfg&gt;::tick()
    participant SCT as ScheduledControlTask
    participant SCM as ScheduledCommsTask
    participant BDT as BackgroundDiagnosticsTask
    participant User as User IScheduledTask(s)

    Main->>Sys: tick(0, now)
    Sys->>SCT: timer.start() → tick(now) → timer.stop()
    SCT->>SCT: evaluateSafety() → processCommands() → app.tick()
    Sys->>BDT: timer.start() → tick(now) → timer.stop()
    BDT->>BDT: watchdogKick(), scan timers
    Sys->>User: timer.start() → tick(now) → timer.stop()
    User->>User: rate-limit check → do work or return
```

---

## 4. Rate-Limiting Model

The kernel calls every task every tick; **tasks are responsible for their own rate-limiting**. The canonical pattern:

```cpp
class MyTask : public IScheduledTask {
public:
    static constexpr SputterMicros kPeriodUs = 10'000; // 100 Hz

    SputterMicros periodUs() const override { return kPeriodUs; }

    void tick(SputterMicros now) override {
        if ((now - m_lastTick) < kPeriodUs)
            return;                   // not yet due — return immediately
        m_lastTick = now;

        // ... do work ...
    }

private:
    SputterMicros m_lastTick{0};
};
```

**Important behaviour**: `m_lastTick` is initialised to `0`. The first tick at `now = 0` evaluates `(0 - 0) < kPeriodUs` → true → skips. The first actual execution fires at `now >= kPeriodUs`. Tests must account for this by starting their first tick at or after the task's period.

### 4.1 Multi-Rate Scheduling

Multiple tasks at different rates can coexist on the same core by using different `kPeriodUs` values. Example: a 100 Hz sampler and a 2 Hz reporter both registered on Core 0. The kernel loop runs at the maximum useful rate (e.g., every 1 ms), and each task skips ticks until its own period expires.

```
Tick rate: 1 ms (1 kHz loop)
Task A: periodUs = 10'000  → fires every 10 ticks  (100 Hz)
Task B: periodUs = 500'000 → fires every 500 ticks (2 Hz)
```

---

## 5. IO_PENDING Pattern

Non-blocking I/O (ADC conversions, SPI reads, thermocouple polls) is modelled as a state machine inside the task, not a kernel-level blocked state. The task declares `isIoPending()` to signal it yielded early; the scheduler does not block it from executing on its next activation.

### 5.1 Why Not a Blocked State

| Concern | Why blocked is wrong | How IO_PENDING solves it |
|---------|---------------------|--------------------------|
| **Safety liveness** | A blocked safety-critical task would stop safety evaluation until the I/O unblocks. | IO_PENDING tasks re-execute every period. Safety evaluation continues regardless of I/O state. |
| **Cooperative scheduler** | True blocking requires an ISR to re-insert the task — complex and breaks the no-ISR-in-scheduler contract. | IO_PENDING is polled on next activation. Zero additional infrastructure. |
| **Determinism** | An indefinitely blocked task creates unpredictable gaps. | A task's period is always known and its next activation is deterministic. |
| **Timeout handling** | A blocked task with a hung device hangs forever. | IO_PENDING task owns its own retry + timeout logic. The scheduler is not involved. |

### 5.2 Task-Side Implementation

```cpp
class AdcPollTask : public IScheduledTask {
public:
    SputterMicros periodUs() const override { return 50'000; }
    bool isIoPending() const override { return m_waitingForAdc; }

    void tick(SputterMicros now) override {
        // Rate-limit when not waiting for IO
        if (!m_waitingForAdc) {
            if ((now - m_lastTick) < periodUs())
                return;
            m_lastTick = now;
        }

        if (m_waitingForAdc) {
            if (m_adc.isConversionReady()) {
                m_lastReading = m_adc.readResult();
                m_waitingForAdc = false;
                m_ioWaitCount = 0;
            } else {
                if (++m_ioWaitCount > kMaxRetries) {
                    reportFault(IoTimeout);
                    m_waitingForAdc = false;
                    m_ioWaitCount = 0;
                }
                return;   // yield — signalled via isIoPending()
            }
        }

        m_adc.startConversion();
        m_waitingForAdc = true;
    }
};
```

**Contract**:
1. Task starts a non-blocking I/O operation and sets `m_waitingForAdc = true`.
2. Task returns early from `tick()`. `isIoPending()` returns true.
3. On every subsequent tick the rate-limit check is **skipped** (task is IO_PENDING), so the task can poll rapidly.
4. When the device signals ready, task reads the result, clears the flag, and resumes normal rate-limiting.
5. Timeout is the task's responsibility. After `kMaxRetries` failed polls the task logs a fault and recovers.

---

## 6. Background Tasks

Background tasks (`IBackgroundTask`) are registered via `SystemBuilder::addBackgroundTask()` and stored in `System<Cfg>::s_backgroundTasks[]`. Currently they are also appended to a core's task list for dispatch (the same flat loop ticks them). The budget declared by `maxBudgetUs()` is observed by `TaskTimer` and checked by `BackgroundDiagnosticsTask`, but the scheduler does not preempt overrunning background tasks.

Future work: Phase 3 — the `SystemScheduler` background ring will dispatch background tasks only in idle gaps between scheduled task activations, eliminating the need to place them in the core task list.

### 6.1 BackgroundDiagnosticsTask

The kernel's built-in diagnostics background task. Responsibilities:

- **Watchdog kick** — calls the injected platform watchdog function each tick.
- **WCET scan** — reads every registered `ITask::timer()` and logs overruns against `declaredWcetUs()` or the auto-profiled max.
- **Memory profiling** — calls `MemoryProfiler` every `kMemCheckInterval` ticks (default 100).

Registered automatically by `SystemBuilder::build()` when an `IUserApplication` is provided.

---

## 7. Kernel State Machine

`KernelState` is defined in `include/sputteros/kernel/KernelState.h` and tracked as `System<Cfg>::s_kernelState`.

```
UNCONFIGURED → CONFIGURED → INITIALIZING → RUNNING
                                             ↓           ↓
                                         SUSPENDING    ABORTING
                                             ↓           ↓
                                         SUSPENDED    ABORTED
                                             ↓
                                       SHUTTING_DOWN → SHUTDOWN
```

| State | Entered by |
|-------|-----------|
| `UNCONFIGURED` | Initial state at power-on |
| `CONFIGURED` | `SystemBuilder::build()` succeeds |
| `INITIALIZING` | `System::init()` called on first core |
| `RUNNING` | `System::init()` completes |
| `ABORTING` | `ControlTask::evaluateSafety()` failure (future wiring) |

`System::tick()` records `s_kernelState` transitions but does not yet gate dispatch on state — all registered tasks are called unconditionally. Full state-gating (e.g., only safety slot runs during `ABORTING`) is planned for Phase 4.

---

## 8. Deadline Infrastructure

`DeadlineTracker` (`include/sputteros/kernel/DeadlineTracker.h`) is a lightweight POD struct for absolute-deadline tracking:

```cpp
struct DeadlineTracker {
    SputterMicros nextActivation{0};
    SputterMicros periodUs{0};
    SputterMicros phaseOffsetUs{0};

    void init(SputterMicros startTime);
    bool isDue(SputterMicros now) const;
    void advance(SputterMicros now);         // skips missed periods
    SputterMicros timeUntilNext(SputterMicros now) const;
};
```

`DeadlineTracker` is available as a utility but is not yet used by `System::tick()`. It is the building block for the planned Cruncher per-slot deadline tracking (Phase 2).

**Missed period behaviour**: `advance()` adds one period, then skips forward until `nextActivation` is in the future. This prevents cascade catch-up storms where many missed periods queue up.

---

## 9. Task Timer Instrumentation

Every `ITask` embeds a `Kernel::TaskTimer`. `System::tick()` calls `timer().start()` before and `timer().stop()` after every `task->tick()` — including early-return ticks. The `TaskTimer` maintains:

| Metric | Accessor | Notes |
|--------|---------|-------|
| Last tick duration | `lastDuration()` | µs |
| Minimum tick duration | `minDuration()` | µs; initialized to `UINT64_MAX` |
| Maximum tick duration | `maxDuration()` | µs |
| Rolling average duration | `getAverageDurationUs()` | Returns 0 if no samples |
| Sample count | `sampleCount()` | `uint32_t`; wraps after ~4.3 B ticks |
| Over-budget flag | `isOverBudget(budgetUs)` | Compares `lastDuration()` to budget |
| Overrun count | `overrunCount()` | Incremented by `BackgroundDiagnosticsTask` |
| Deadline-miss count | `deadlineMissCount()` | Incremented for period violations |
| Duration histogram | `histogram()` | 8 buckets × 512 µs — see below |
| Approximate percentile | `percentileUs(p)` | Linear interpolation within bucket |

`BackgroundDiagnosticsTask` reads these metrics to detect WCET violations. In tests, `task.timer().sampleCount() > 0` confirms that the task was dispatched at all.

### 9.1 Histogram Distribution

Each `stop()` call places the elapsed duration into one of 8 fixed-width buckets:

| Bucket | Range |
|--------|-------|
| 0 | [0, 512) µs |
| 1 | [512, 1024) µs |
| 2 | [1024, 1536) µs |
| 3 | [1536, 2048) µs |
| 4 | [2048, 2560) µs |
| 5 | [2560, 3072) µs |
| 6 | [3072, 3584) µs |
| 7 | ≥ 3584 µs (overflow catch-all) |

The bucket width of **512 µs** (2⁹) ensures the index computation `elapsed >> 9` is a single right-shift on Cortex-M0+ rather than a software division.

`percentileUs(float p)` walks the histogram and linearly interpolates within the containing bucket. It is an off-hot-path, on-demand operation — not called per tick.

### 9.2 Performance Snapshot

`System<Cfg>::snapshot()` aggregates all per-task timers together with per-core utilization (`CoreUtilizationTracker`), scheduler health (`SchedulerHealthMetrics`), queue depth (`QueueDepthMonitor`), and memory profiling into a `PerformanceSnapshot` POD value. The struct is ~1.2 KiB on the stack for a 16-task, 4-core configuration — call `snapshot()` from a background or top-level context rather than from inside a time-critical tick.

`PerformanceFormatter::formatKeyValue()` and `formatCSV()` convert a snapshot to text in a caller-supplied `char` buffer using integer arithmetic (zero heap, no `printf`).

---

## 10. Builder Registration API

```cpp
SystemBuilder<Cfg> builder(&app, monitors, monitorCount);
builder.setStream(&stream)
       .setWatchdogKick(kickFn)
       .setClockSource(clockFn);

// Scheduled tasks on a specific core
builder.core(0).addScheduledTask(&myTask);

// Background tasks (no core affinity)
builder.addBackgroundTask(&myBgTask);

// Validate + populate System<Cfg>
BuildResult result = builder.build();
```

### 10.1 Build-time Validation

`build()` performs these checks before populating `System<Cfg>`:

1. Each `IScheduledTask`'s `periodUs()` ≥ `CfgMinSchedulePeriodUs<Cfg>::value` (default 10 µs).
2. No duplicate task pointer across cores.
3. Each task's `validateDependencies()` returns true.
4. At least one core has tasks (or kernel tasks are being created).

### 10.2 Automatic Kernel Task Placement

When `app` is non-null, `build()` creates and pre-registers:

| Task | Core | Position |
|------|------|---------|
| `ScheduledControlTask` | 0 | Slot 0 (prepended) |
| `ScheduledCommsTask` | 1 (or 0, single-core) | Slot 0 (prepended) |
| `BackgroundDiagnosticsTask` | Shared (prepended to core list) | First background |

User tasks added via `addScheduledTask()` follow the kernel tasks in tick order.

---

## 11. ConfigTraits Extensions

New optional config fields added to the `Cfg` struct contract, with SFINAE extractors providing defaults:

| Field | Default | Purpose |
|-------|---------|---------|
| `kMinSchedulePeriodUs` | 10 µs | Floor for `IScheduledTask::periodUs()` — enforced at build time |
| `kStrictWCET` | `false` | If true, `forceSafeAbort()` on WCET violation (future use) |
| `kIsrContextBudgetUs[kCoreCount]` | `{0, 0, ...}` | Per-core ISR overhead budget for utilization accounting (future use) |

---

## 12. Testing Coverage

### Unit Tests

| Test Suite | File | What it Tests |
|------------|------|--------------|
| `DeadlineTrackerTest` | `test_DeadlineTracker.cpp` | Phase offset, `isDue()`, `advance()`, missed-period skip |
| `IScheduledTaskTest` | `test_IScheduledTask.cpp` | Type markers, periodUs contract, isIoPending default |
| `IBackgroundTaskTest` | `test_IBackgroundTask.cpp` | Type markers, maxBudgetUs default |
| `MultiRateDataFlowTest` | `test_MultiRateDataFlow.cpp` | Multi-rate task accumulation, drain, sawtooth wrap, rate-limiting edge cases |
| `IoPendingPatternTest` | `test_IoPendingPattern.cpp` | IO_PENDING state machine: start → poll → read → timeout → recovery |
| `CfgMinSchedulePeriodUs` | `test_ConfigTraits.cpp` | Default and override values |
| `CfgStrictWCET` | `test_ConfigTraits.cpp` | Default and override |
| `CfgIsrContextBudgetUs` | `test_ConfigTraits.cpp` | Default and override |

### System Tests

| Test Suite | File | What it Tests |
|------------|------|--------------|
| `MultiRatePipelineTest` | `test_MultiRatePipeline.cpp` | Build → init → tick loop with multi-rate tasks; sample counts, drain, TaskTimer |
| `IoPendingPipelineTest` | `test_IoPendingPipeline.cpp` | Full kernel IO_PENDING cycle: start → yield → read; timeout detection; background coexistence |

### Example Projects

| Project | What it Demonstrates |
|---------|---------------------|
| `multirate` | Single-core multi-rate dispatch: 100 Hz sampler + 2 Hz reporter + background idle counter |
| `sensorpoll` | IO_PENDING non-blocking ADC polling with simulated conversion delay |
| `lifecycle` | Dual-core AMP with kernel state observation and task lifecycle hooks |
| `heartbeat` | Minimal single-task sanity check |
| `pingpong` | Dual-core command queue round-trip |

All example projects are registered with CTest and run as part of `make exampleProjects`.
