# AMP Partitioned Scheduling Design

A design document for adding Asymmetric Multi-Processing (AMP) partitioned scheduling to SputterOS: per-core "Cruncher" fast-path schedulers and a system-wide SMP cooperative scheduler for background work.

---

## Table of Contents

1. [Motivation & Goals](#motivation--goals)
2. [Architectural Overview](#architectural-overview)
3. [Alternatives Considered](#alternatives-considered)
4. [Cruncher — Per-Core Partitioned Scheduler](#cruncher--per-core-partitioned-scheduler)
5. [SystemScheduler — SMP Background Scheduler](#systemscheduler--smp-background-scheduler)
6. [Scheduling Primitives & Time Model](#scheduling-primitives--time-model)
7. [Task Model Changes](#task-model-changes)
8. [ConfigTraits Extensions](#configtraits-extensions)
9. [Strengths & Weaknesses](#strengths--weaknesses)
10. [Groundlaying Changes](#groundlaying-changes)
    - [10.1 Kernel State Machine](#101-kernel-state-machine)
    - [10.2 Task State Machine](#102-task-state-machine)
    - [10.3 Timer & Deadline Infrastructure](#103-timer--deadline-infrastructure)
    - [10.4 System::tick() Refactor](#104-systemtick-refactor)
    - [10.5 ITask Hierarchy Updates](#105-itask-hierarchy-updates)
    - [10.6 SystemBuilder Updates](#106-systembuilder-updates)
    - [10.7 ConfigTraits & Validator Updates](#107-configtraits--validator-updates)
    - [10.8 DiagnosticsTask Scheduler Awareness](#108-diagnosticstask-scheduler-awareness)
11. [Implementation Phases](#implementation-phases)
12. [Testing Strategy](#testing-strategy)

---

## 1. Motivation & Goals

### Current Limitations

The existing `System<Cfg>::tick()` iterates a flat array of `ITask*` per core in registration order. Every task executes every tick, unconditionally. There is no notion of:

- **Task period / rate**: All tasks tick at the loop rate (whatever the platform delivers).
- **Deadline enforcement**: `DiagnosticsTask` detects overruns post-hoc; it cannot preempt or defer.
- **Gap utilization**: CPU idle time between the end of a tick batch and the start of the next period is wasted.
- **Mixed-criticality**: All tasks on a core share a single priority level.

This is acceptable for the current 3-task kernel (ControlTask 100 Hz, CommsTask best-effort, DiagnosticsTask best-effort), but breaks down when the user needs:

- Multiple periodic control loops at different rates (e.g., 10 kHz PID inner loop, 100 Hz outer loop, 1 Hz logging).
- Aperiodic/event-driven tasks that should only run when triggered.
- Background tasks that fill idle CPU time without jeopardizing deterministic tasks.

### Design Goals

| Goal | Target |
|------|--------|
| Minimum scheduling granularity | **10 µs** (100 kHz tick resolution) |
| Zero heap allocation | All scheduler state in `inline static` storage |
| Deterministic worst-case on Core 0 | No priority inversion, no unbounded loops |
| Clean break — port all tasks forward | `ICriticalTask` and `IAsyncTask` are retired. All tasks are redesigned as `IScheduledTask` or `IBackgroundTask`. No legacy wrappers or auto-conversion. |
| Per-core independence | Each Cruncher runs autonomously; no cross-core locking in the fast path |
| Gap utilization | SMP scheduler fills idle time with background work |
| Lightweight | < 2 KiB RAM overhead per core for scheduler state |

---

## 2. Architectural Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          System<Cfg> Singleton                         │
│                                                                        │
│  ┌────────────────────────┐     ┌────────────────────────┐             │
│  │   Core 0 — Cruncher    │     │   Core 1 — Cruncher    │             │
│  │                        │     │                        │             │
│  │  Slot Table (static)   │     │  Slot Table (static)   │             │
│  │  ┌──────┬──────┬─────┐ │     │  ┌──────┬──────┬─────┐ │             │
│  │  │Slot 0│Slot 1│ ... │ │     │  │Slot 0│Slot 1│ ... │ │             │
│  │  │10µs  │100µs │     │ │     │  │50µs  │1ms   │     │ │             │
│  │  │PID   │Ctrl  │     │ │     │  │Comms │Diag  │     │ │             │
│  │  └──────┴──────┴─────┘ │     │  └──────┴──────┴─────┘ │             │
│  │                        │     │                        │             │
│  │  ┌──────────────────┐  │     │  ┌──────────────────┐  │             │
│  │  │  Deadline Queue  │  │     │  │  Deadline Queue  │  │             │
│  │  │  (sorted static  │  │     │  │  (sorted static  │  │             │
│  │  │   ring buffer)   │  │     │  │   ring buffer)   │  │             │
│  │  └──────────────────┘  │     │  └──────────────────┘  │             │
│  └────────────────────────┘     └────────────────────────┘             │
│                                                                        │
│  ┌──────────────────────────────────────────────────────────┐          │
│  │            SystemScheduler (SMP — runs in gap time)      │          │
│  │                                                           │          │
│  │  Background Task Ring    Runs on ANY core with idle time  │          │
│  │  ┌──────┬──────┬──────┐  Round-robin, cooperative yield   │          │
│  │  │BG 0  │BG 1  │BG 2  │  Time-boxed per invocation       │          │
│  │  │Telem │MemChk│User  │                                   │          │
│  │  └──────┴──────┴──────┘                                   │          │
│  └──────────────────────────────────────────────────────────┘          │
└─────────────────────────────────────────────────────────────────────────┘
```

**Two-tier scheduling model:**

1. **Cruncher (per-core, AMP)** — A lightweight, statically-allocated, deadline-driven scheduler local to each core. Owns a fixed slot table of periodic tasks with individually configured periods. Executes the highest-priority ready task at each scheduling point. No cross-core communication in the fast path.

2. **SystemScheduler (system-wide, SMP)** — A cooperative round-robin scheduler for low-priority background tasks. Runs in the gap between the Cruncher's last ready task and the next deadline. Can execute on any core that has idle time. Time-boxed to prevent starvation of real-time tasks.

---

## 3. Alternatives Considered

### 3.1 Full Preemptive RTOS (FreeRTOS / Zephyr)

| Aspect | Assessment |
|--------|------------|
| **Pros** | Mature, well-tested, full priority preemption, rich ecosystem |
| **Cons** | Heap allocation (task stacks), priority inversion risk, ~10-30 KiB RAM overhead, violates zero-heap constraint, non-trivial porting to the existing `inline static` model, overkill for 2-core bare-metal |
| **Verdict** | **Rejected.** Contradicts core SputterOS design principles (zero-heap, deterministic, no RTOS dependency). |

### 3.2 Rate-Monotonic Scheduling (RMS) with Static Priorities

| Aspect | Assessment |
|--------|------------|
| **Pros** | Well-understood theory, provable schedulability via utilization bound |
| **Cons** | Requires preemption for optimality (SputterOS is cooperative), worst-case response time analysis is more complex without preemption, priority assignment is global (conflicts with AMP partitioning) |
| **Verdict** | **Partially adopted.** The Cruncher uses RMS-inspired static priority ordering (shorter period = higher priority) but remains non-preemptive within a single tick. |

### 3.3 Earliest Deadline First (EDF)

| Aspect | Assessment |
|--------|------------|
| **Pros** | Optimal utilization (100% for preemptive uniprocessor), adapts to transient overloads |
| **Cons** | Dynamic priority requires sorted insertion (O(n) per activation), harder to analyze worst-case without preemption, domino failure under overload (all tasks miss deadlines simultaneously) |
| **Verdict** | **Rejected for Cruncher.** The static slot table with fixed priorities is simpler, cheaper, and sufficient. EDF's higher utilization bound doesn't justify the complexity for ≤ 32 tasks per core. Revisit if utilization becomes a bottleneck. |

### 3.4 Time-Triggered Architecture (TTA)

| Aspect | Assessment |
|--------|------------|
| **Pros** | Fully deterministic, zero jitter, trivial schedulability analysis |
| **Cons** | Inflexible (fixed schedule must be recomputed offline for any change), wastes CPU on idle slots, poor gap utilization, cannot accommodate aperiodic/event-driven tasks |
| **Verdict** | **Partially adopted.** The Cruncher's slot table is a lightweight TTA with dynamic skip logic — slots that aren't due are skipped rather than busy-waited, and gap time is reclaimed by the SystemScheduler. |

### 3.5 Pure Cooperative (current model, extended with periods)

| Aspect | Assessment |
|--------|------------|
| **Pros** | Simplest implementation, no scheduler state, just add period checks to each task |
| **Cons** | No gap utilization, no mixed-criticality, ordering depends on registration sequence, long tasks block all subsequent tasks, no deadline awareness |
| **Verdict** | **Rejected as final design.** This is effectively what the current `System::tick()` does. Adding period checks alone doesn't solve gap utilization or priority ordering. |

---

## 4. Cruncher — Per-Core Partitioned Scheduler

### 4.1 Concept

Each core owns exactly one `Cruncher` instance. The Cruncher maintains a **slot table** — a fixed-size array of `ScheduleSlot` structs, each binding a task pointer to a period and tracking its next activation deadline. The slot table is sorted by priority at build time (shortest period = highest priority, per RMS).

At each scheduling point (called from the platform's tick loop or timer ISR), the Cruncher:

1. Reads the current time from `MicrosecondSource`.
2. Scans the slot table from highest to lowest priority.
3. For each slot whose `nextActivation <= now`: marks it **READY**.
4. Executes the highest-priority READY slot's task.
5. After execution: advances that slot's `nextActivation` by its period.
6. If no READY slots remain and gap time is available: yields to `SystemScheduler`.

### 4.2 ScheduleSlot

```cpp
struct ScheduleSlot {
    ITask*        task;             // Bound task (non-owning)
    SputterMicros periodUs;         // Activation period in microseconds (≥ 10)
    SputterMicros nextActivation;   // Absolute time of next scheduled run
    SputterMicros wcet;             // Worst-case execution time (observed or declared)
    SlotState     state;            // IDLE, READY, RUNNING, OVERRUN
    uint8_t       priority;         // Lower value = higher priority (auto-assigned by period)
    bool          enabled;          // Runtime enable/disable without removing
};
```

**Static allocation**: `ScheduleSlot slots[kMaxSlotsPerCore]` inside `Cruncher<kMaxSlots>`. Default `kMaxSlotsPerCore = 32`.

### 4.3 Scheduling Algorithm

```
cruncher_tick(now):
    // Phase 1: Activation scan — mark due slots READY
    for slot in slots[0..slotCount]:
        if slot.enabled AND slot.nextActivation <= now:
            slot.state = READY

    // Phase 2: Dispatch — run highest-priority READY slot
    for slot in slots[0..slotCount]:        // sorted by priority (ascending = highest first)
        if slot.state == READY:
            slot.state = RUNNING
            slot.task->timer().start()
            slot.task->tick(now)
            slot.task->timer().stop()
            
            elapsed = slot.task->timer().lastDuration()
            if elapsed > slot.wcet:
                slot.wcet = elapsed          // update observed WCET
            if elapsed > slot.periodUs:
                slot.state = OVERRUN         // flag for diagnostics
                errorLogger.log(TASK_OVERRUN, now, elapsed)
            else:
                slot.state = IDLE
            
            // Advance deadline (absolute, not relative to completion)
            slot.nextActivation += slot.periodUs
            
            // If we fell behind (nextActivation still in the past), skip missed periods
            while slot.nextActivation <= now:
                slot.nextActivation += slot.periodUs
                errorLogger.log(DEADLINE_MISS, now, slot.priority)
            
            return DISPATCHED

    // Phase 3: No READY slots — yield gap time
    return GAP_AVAILABLE
```

### 4.4 Priority Assignment

Priorities are auto-assigned at build time using Rate-Monotonic ordering:

```
priority = 0  →  shortest period  (highest priority)
priority = 1  →  next shortest
...
priority = N  →  longest period   (lowest priority)
```

Users may override via explicit priority in `CoreBuilder::addScheduledTask()`, but the default is RMS-optimal.

### 4.5 Overrun & Deadline Miss Policy

| Event | Action |
|-------|--------|
| **Task exceeds its period** | Log `TASK_OVERRUN` to `ErrorLogger`. Slot transitions to `OVERRUN` state. Next activation is still advanced to maintain phase alignment. |
| **Deadline miss (nextActivation in the past after advance)** | Skip missed periods until `nextActivation` is in the future. Log `DEADLINE_MISS` for each skipped period. This prevents cascading catch-up storms. |
| **Task exceeds user-declared WCET** | Update observed WCET. If a `kStrictWCET` config flag is set, trigger `forceSafeAbort()` on safety-critical tasks. |
| **Task throws / faults** | Not applicable (C++17, no exceptions in kernel). |

### 4.6 Safety Integration

The Cruncher preserves the existing safety contract:

- **Slot 0 on Core 0 is always reserved for `ControlTask`** (or its successor). Safety evaluation (`evaluateSafety()`) runs as the first action inside `ControlTask::tick()`, exactly as today.
- If safety fails, `forceSafeAbort()` fires and the Cruncher respects the abort state — no further user task slots are dispatched until the kernel state machine re-enters a safe state.

---

## 5. SystemScheduler — SMP Background Scheduler

### 5.1 Concept

A single, system-wide cooperative scheduler for background / low-priority tasks. It runs **only in gap time** — the CPU cycles remaining after all Cruncher slots have been serviced in a scheduling round.

The SystemScheduler is invoked by the Cruncher when `cruncher_tick()` returns `GAP_AVAILABLE`. It is time-boxed: it will not run a background task if there isn't enough time before the next Cruncher deadline.

### 5.2 Background Task Ring

```cpp
struct BackgroundEntry {
    ITask*        task;            // Non-owning pointer
    SputterMicros maxBudgetUs;     // Maximum execution time per invocation
    SputterMicros lastRunTime;     // Last time this task was invoked
    bool          enabled;         // Runtime enable/disable
};
```

Storage: `BackgroundEntry ring[kMaxBackgroundTasks]` with a round-robin index. Default `kMaxBackgroundTasks = 16`.

### 5.3 Algorithm

```
system_scheduler_tick(now, gapBudgetUs):
    // gapBudgetUs = time until next Cruncher deadline on this core
    
    remaining = gapBudgetUs
    startIdx = roundRobinIdx
    
    do:
        entry = ring[roundRobinIdx]
        roundRobinIdx = (roundRobinIdx + 1) % count
        
        if entry.enabled AND entry.maxBudgetUs <= remaining:
            entry.task->timer().start()
            entry.task->tick(now)
            entry.task->timer().stop()
            
            elapsed = entry.task->timer().lastDuration()
            entry.lastRunTime = now
            remaining -= elapsed
            
            if remaining < kMinGapSliceUs:    // e.g. 10 µs minimum
                break
    while roundRobinIdx != startIdx
```

### 5.4 Cross-Core Execution

Background tasks are **not pinned** to a specific core. The SystemScheduler ring is shared (read-only structure, per-core round-robin index). A background task may execute on whichever core has gap time first.

**Thread safety**: Background tasks must be stateless or use their own synchronization if they access shared state. The SystemScheduler will never run the same background task concurrently on two cores — a per-entry `std::atomic<bool> running` flag provides mutual exclusion without locks:

```cpp
if (!entry.running.exchange(true, std::memory_order_acquire))
{
    entry.task->tick(now);
    entry.running.store(false, std::memory_order_release);
}
```

### 5.5 Default Background Tasks

| Task | Role |
|------|------|
| `DiagnosticsTask` | Watchdog kick, timer scan, memory profiling — moved from fixed Core 1 slot to background |
| `TelemetryDrain` | Drain `TelemetryLogger` buffer to stream |
| User-registered tasks | Anything the user adds via `builder.addBackgroundTask()` |

---

## 6. Scheduling Primitives & Time Model

### 6.1 10 µs Granularity

The scheduler operates in units of `SputterMicros` (uint64_t microseconds). The minimum scheduling period is **10 µs** (100 kHz). This is enforced at build time:

```cpp
static_assert(slot.periodUs >= 10, "Minimum scheduling period is 10 µs");
```

On an RP2350 at 150 MHz, 10 µs = 1,500 clock cycles. This is enough for a minimal task (read ADC, update atomic, return) but not for complex processing. Users are responsible for ensuring their task WCET fits within the declared period.

### 6.2 Absolute Deadline Model

Deadlines are **absolute timestamps**, not relative offsets. When a slot is activated:

```
nextActivation(n+1) = nextActivation(n) + periodUs
```

This prevents drift accumulation. If processing takes longer than expected, the next deadline is still referenced to the original phase, not to the completion time.

### 6.3 Phase Alignment

All slots start with `nextActivation = buildTime + phaseOffsetUs`. Phase offsets can be assigned automatically to spread activations:

```
slot[i].nextActivation = startTime + (i * periodUs / slotCount)
```

Or explicitly by the user for precise hardware timing requirements.

### 6.4 Clock Source

The Cruncher reads time from the same injectable `MicrosecondSource` used by the existing `SystemTimer`. No new clock abstraction is needed. The `MicrosecondSource` must:

- Return monotonically increasing `uint64_t` microseconds.
- Be callable from any core without locking.
- Have ≤ 1 µs resolution (most hardware timers satisfy this).

### 6.5 How Task Execution Time is Calculated, Determined, and Set

Task execution time is central to the scheduler. There are three distinct concepts, obtained through different mechanisms:

#### 6.5.1 Measured Execution Time (Runtime — System Clock)

The primary method. `TaskTimer` wraps every `tick()` call with `start()` / `stop()` using the injected `MicrosecondSource`:

```
timer.start()          // → reads clock source → stores m_start
task->tick(now)        // → task executes
timer.stop()           // → reads clock source → elapsed = now - m_start
                       //   updates lastDuration, maxDuration, average, sampleCount
```

This is **post-hoc observation** — you learn what the task cost after it ran. The Cruncher uses `lastDuration()` to detect overruns and update the observed WCET.

**Accuracy depends on clock source resolution.** On RP2350 (1 µs timer peripheral), measurement error is ±1 µs. On a platform with only a 1 ms tick, tasks below 1 ms would all measure as 0 — the scheduler would be blind.

#### 6.5.2 Declared WCET (Build-Time — User-Specified)

Each `IScheduledTask` can declare its expected worst-case execution time:

```cpp
SputterMicros declaredWcetUs() const override { return 50; } // 50 µs max
```

This is a **contract, not a measurement**. The user asserts "this task will never take more than 50 µs." The scheduler uses it for:

- **Build-time utilization analysis**: `sum(declaredWcet[i] / period[i])` tells whether the schedule is feasible before any code runs.
- **Runtime enforcement**: If `kStrictWCET` is enabled and measured time exceeds declared WCET, the scheduler can trigger `forceSafeAbort()` for safety-critical slots, or log a fault for others.

If `declaredWcetUs()` returns 0 (the default), the scheduler falls back to auto-profiling from measured data.

#### 6.5.3 Profiled WCET (Runtime — Auto-Observed)

When no declared WCET is provided, the `ScheduleSlot::wcet` field tracks the **observed maximum** from `TaskTimer::maxDuration()`. This is the running high-water mark across all invocations:

```
if (elapsed > slot.wcet)
    slot.wcet = elapsed;    // auto-profile: update observed WCET
```

The profiled WCET grows monotonically during operation. It is useful for diagnostics and runtime utilization reporting, but unreliable for schedulability guarantees — it only reflects the worst case *seen so far*, not the theoretical worst case.

#### 6.5.4 Alternatives to the System Clock

| Method | How It Works | Pros | Cons | Recommendation |
|--------|-------------|------|------|----------------|
| **System clock (`MicrosecondSource`)** | Reads platform timer peripheral before/after `tick()` | Universal, no extra hardware, ±1 µs on most MCUs | Two clock reads per task per dispatch (~0.1 µs overhead); blind if clock resolution > scheduling granularity | **Primary method.** Always available, sufficient for ≥ 10 µs scheduling. |
| **Hardware cycle counter (DWT / SysTick)** | Read ARM `DWT_CYCCNT` (Cortex-M) or RISC-V `mcycle` CSR directly | Sub-nanosecond resolution, zero-overhead read (single instruction), independent of timer peripheral | Architecture-specific, must convert cycles → µs (division by clock MHz), unavailable on some cores (e.g., RP2040 has no DWT on Core 1) | **Optional fast-path override.** Expose as an alternative `MicrosecondSource` for platforms that support it. No scheduler changes needed — just wire a different clock function. |
| **Hardware timer capture / compare** | Configure a timer peripheral to fire a compare-match interrupt at `nextActivation` | Zero polling overhead — the hardware triggers the scheduler at exactly the right time; sub-µs jitter | Consumes a timer peripheral (scarce resource on small MCUs); ISR context complexity; compare-match setup cost per rescheduling point | **Future enhancement for ISR-driven scheduling.** Would replace the polling Cruncher loop with interrupt-driven dispatch. Out of scope for Phase 1-3 but architecturally compatible — the Cruncher's `tick()` could be called from a timer ISR instead of a polling loop. |
| **GPIO toggle + oscilloscope** | Toggle a GPIO pin on `tick()` entry/exit, measure pulse width externally | Most accurate real-world measurement; includes cache/interrupt overhead that software timers miss | External equipment required; not usable at runtime; manual / offline only | **Validation tool.** Use during hardware bring-up to calibrate declared WCETs. Not a scheduler input. |
| **Offline static analysis (WCET tools)** | Tools like aiT, Chronos, or OTAWA analyze the binary to compute a provable WCET bound | Provable upper bound — no measurement uncertainty | Extremely difficult to apply to C++17 with templates, virtual dispatch, and pipeline-dependent MCUs; expensive commercial tooling | **Not practical for SputterOS.** The codebase's heavy template usage and virtual dispatch make static WCET analysis infeasible. Rely on measured + declared WCET. |

#### 6.5.5 Recommended Approach

Use a layered strategy:

1. **Declare WCET** for all safety-critical scheduled tasks during development. Base the declaration on measured data from profiling runs + a safety margin (e.g., 2× measured max).
2. **Measure at runtime** via `TaskTimer` always. This is free (two clock reads) and feeds the diagnostics system.
3. **Validate with hardware** (GPIO + scope) during hardware bring-up for the innermost loop tasks where 10 µs budgets leave slim margins.
4. **Build-time utilization check** uses declared WCETs. If any task lacks a declaration, the tool substitutes the profiled WCET (if available) or flags a warning.

---

## 7. Task Model Changes

### 7.1 Task Classification — Clean Break

`ICriticalTask` and `IAsyncTask` are **retired**. Every task in the system is ported forward to one of two new base classes. There is no legacy wrapper, no auto-conversion, and no `addTask()` fallback.

```
ITask (base — lifecycle + device deps + timer)
├── IScheduledTask    (Cruncher-managed, has period + priority + WCET)
└── IBackgroundTask   (SystemScheduler-managed, cooperative, time-boxed)
```

`ITask` remains as the root interface providing `init()`, `tick()`, `timer()`, and device dependency tracking. It is never registered directly — `SystemBuilder` only accepts `IScheduledTask*` or `IBackgroundTask*`.

### 7.2 IScheduledTask

```cpp
class IScheduledTask : public ITask {
public:
    virtual SputterMicros periodUs() const = 0;
    virtual SputterMicros declaredWcetUs() const { return 0; }  // 0 = auto-profile
    virtual uint8_t schedulePriority() const { return 0xFF; }   // 0xFF = auto-assign (RMS)
    bool isScheduled() const final { return true; }
    bool isBackground() const final { return false; }
};
```

### 7.3 IBackgroundTask

```cpp
class IBackgroundTask : public ITask {
public:
    virtual SputterMicros maxBudgetUs() const { return 1000; }  // default 1 ms cap
    bool isBackground() const final { return true; }
    bool isScheduled() const final { return false; }
};
```

### 7.4 Kernel Task Port-Forward

All three kernel tasks are redesigned to use the new hierarchy directly:

| Existing Task | New Base | Ported As | Core | Period |
|---------------|----------|-----------|------|--------|
| `ControlTask<Cfg>` | `IScheduledTask` | `ScheduledControlTask<Cfg>` | Core 0, Slot 0 (highest priority) | `kControlBudgetUs` |
| `CommsTask<Cfg>` | `IScheduledTask` | `ScheduledCommsTask<Cfg>` | Core 1, Slot 0 | `kCommsBudgetUs` |
| `DiagnosticsTask` | `IBackgroundTask` | `BackgroundDiagnosticsTask` | SystemScheduler ring | `kDiagsBudgetUs` budget |

`ICriticalTask` and `IAsyncTask` are deleted from the codebase. Core affinity is no longer encoded in the type system — it is specified at registration time via `core(N).addScheduledTask()`. The builder enforces that the safety-critical `ScheduledControlTask` is always Slot 0 on Core 0.

### 7.5 User Task Migration

Users must port their tasks:

- **Periodic work** (control loops, sensor polling, protocol handlers) → subclass `IScheduledTask`, implement `periodUs()` and optionally `declaredWcetUs()`.
- **Best-effort work** (logging, telemetry drain, memory checks, UI updates) → subclass `IBackgroundTask`, implement `maxBudgetUs()`.
- **`core(N).addTask(ITask*)` is removed.** All registration goes through `addScheduledTask()` or the system-level `addBackgroundTask()`.

---

## 8. ConfigTraits Extensions

New optional config fields (with defaults via SFINAE extraction):

```cpp
// Scheduling
static constexpr SputterMicros kMinSchedulePeriodUs = 10;       // Floor for slot periods
static constexpr std::size_t   kMaxSlotsPerCore     = 32;       // Cruncher slot table size
static constexpr std::size_t   kMaxBackgroundTasks  = 16;       // SystemScheduler ring size
static constexpr SputterMicros kCommsBudgetUs       = 1000;     // CommsTask period (1 kHz)
static constexpr SputterMicros kDiagsBudgetUs       = 10000;    // DiagnosticsTask background budget
static constexpr SputterMicros kMinGapSliceUs       = 10;       // Minimum gap for bg task dispatch
static constexpr bool          kStrictWCET           = false;    // Abort on WCET violation

// Kernel State Machine
static constexpr bool          kKernelStateMachine  = true;     // Enable built-in kernel FSM
```

---

## 9. Strengths & Weaknesses

### Strengths

| Strength | Detail |
|----------|--------|
| **Deterministic fast path** | Cruncher scan is O(n) over a small fixed array (≤ 32 slots). No dynamic allocation, no locking, no priority inversion. Worst-case scheduling overhead is bounded and predictable. |
| **10 µs resolution** | Enables high-frequency inner control loops (PID at 10-100 kHz) alongside slower outer loops (state machine at 100 Hz), all on the same core with priority ordering. |
| **Gap utilization** | Background tasks automatically fill idle CPU time. No wasted cycles sitting in a `while (!ready)` spin loop. |
| **Per-core isolation** | Cruncher instances are completely independent. A misbehaving task on Core 1 cannot affect Core 0's scheduling. True AMP partitioning. |
| **Zero heap** | All state is `inline static` arrays sized by compile-time constants. Fits the existing SputterOS memory model. |
| **Clean task model** | Two base classes (`IScheduledTask`, `IBackgroundTask`) replace four (`ITask`, `ICriticalTask`, `IAsyncTask` + new). No ambiguity about which type to subclass. No hidden auto-wrapping behavior. |
| **Observable** | Every slot tracks WCET, overrun count, deadline misses. DiagnosticsTask (now a background task) can report scheduling health over telemetry. |
| **Simple mental model** | "Cruncher = fast periodic tasks, SystemScheduler = slow background tasks." No complex priority inheritance, no ceiling protocols, no RTOS jargon. |

### Weaknesses

| Weakness | Mitigation |
|----------|------------|
| **Non-preemptive** | A long-running task blocks all lower-priority tasks until it completes. **Mitigation:** WCET enforcement + period validation at build time. Users must ensure task WCET < period. Strict mode can abort violators. |
| **No dynamic task creation** | All tasks must be registered at build time. Cannot spawn tasks at runtime. **Mitigation:** Enable/disable flag per slot allows runtime activation/deactivation without dynamic allocation. |
| **Priority inversion within Cruncher** | If a high-priority task is blocked (e.g., waiting on an atomic flag set by a lower-priority task on the same core), the Cruncher cannot resolve this because it's non-preemptive. **Mitigation:** Design constraint — tasks on the same core should not have data dependencies. Use the inter-core `LockFreeQueue` for cross-priority communication. |
| **Background task starvation** | If Cruncher slots consume 100% of CPU time, background tasks never run. **Mitigation:** Build-time utilization check warns if sum of (WCET/period) exceeds a threshold (e.g., 80%). Reserve explicit gap budget. |
| **Breaking change** | All existing user tasks must be ported to `IScheduledTask` or `IBackgroundTask`. **Mitigation:** The new hierarchy is simpler (two choices, not four), and the migration is mechanical — subclass swap + implement `periodUs()` or `maxBudgetUs()`. |
| **O(n) scan per tick** | For 32 slots, this is ~32 comparisons per scheduling point — negligible on any modern MCU. Only becomes a concern if `kMaxSlotsPerCore` is raised dramatically. |
| **Cross-core background task synchronization** | Background tasks running on different cores need their own synchronization. **Mitigation:** Atomic running flag prevents concurrent execution. Users own any additional shared state protection. |

---

## 10. Groundlaying Changes

These are prerequisite changes to the existing codebase that must be completed before the Cruncher and SystemScheduler can be implemented.

### 10.1 Kernel State Machine

**Current state**: The system lifecycle is managed by `MultiCoreSync<N>` (UNBORN → INIT → READY → SHUTDOWN/ERROR) which is per-core. There is no system-wide operational state machine governing what the kernel is *doing* (idle, running, aborting, etc.). The user's `IProcessState` is optional and lives outside the kernel.

**Required change**: Introduce a kernel-owned `KernelState` enum and state machine inside `System<Cfg>`.

```cpp
enum class KernelState : uint8_t {
    UNCONFIGURED,   // Before build()
    CONFIGURED,     // After build(), before init()
    INITIALIZING,   // During init() — tasks being initialized
    RUNNING,        // Normal operation — Cruncher dispatching
    SUSPENDING,     // Graceful pause requested — drain in-flight tasks
    SUSPENDED,      // All Crunchers paused — background tasks may still run
    ABORTING,       // Safety abort in progress
    ABORTED,        // Safe state reached — waiting for operator
    SHUTTING_DOWN,  // Orderly shutdown — draining tasks
    SHUTDOWN        // Terminal state
};
```

**Rationale**: The Cruncher needs to know whether to dispatch tasks. During `ABORTING`, only the safety-critical slot (ControlTask abort sequence) should run. During `SUSPENDED`, background tasks can run but Cruncher slots are paused.

**Location**: New `inline static KernelState s_kernelState{KernelState::UNCONFIGURED}` in `System<Cfg>`. Transitions are triggered by:
- `build()` → CONFIGURED
- `init()` → INITIALIZING → RUNNING
- `forceSafeAbort()` on any safety monitor → ABORTING → ABORTED
- User-initiated pause → SUSPENDING → SUSPENDED
- User-initiated resume → RUNNING
- Shutdown request → SHUTTING_DOWN → SHUTDOWN

**State transition enforcement**: Only valid transitions are permitted. Invalid transition requests log an `INVALID_STATE_TRANSITION` error and are ignored. The transition table is a compile-time `constexpr` array.

```
UNCONFIGURED → CONFIGURED                  (build)
CONFIGURED → INITIALIZING                  (init)
INITIALIZING → RUNNING                     (init complete)
RUNNING → SUSPENDING                       (pause request)
RUNNING → ABORTING                         (safety failure)
RUNNING → SHUTTING_DOWN                    (shutdown request)
SUSPENDING → SUSPENDED                     (all tasks drained)
SUSPENDED → RUNNING                        (resume request)
SUSPENDED → SHUTTING_DOWN                  (shutdown from paused)
ABORTING → ABORTED                         (safe state reached)
ABORTED → RUNNING                          (operator recovery)
ABORTED → SHUTTING_DOWN                    (shutdown from abort)
SHUTTING_DOWN → SHUTDOWN                   (all tasks stopped)
```

### 10.2 Task State Machine

**Current state**: Tasks have no explicit state — they are either registered or not. `ITask::init()` and `ITask::tick()` are called unconditionally.

**Required change**: Add a `TaskState` tracked per-slot by the Cruncher.

```cpp
enum class TaskState : uint8_t {
    UNINITIALIZED,  // Registered but init() not yet called
    IDLE,           // Initialized, waiting for next activation
    READY,          // Activation time reached, waiting for dispatch
    RUNNING,        // Currently executing tick()
    OVERRUN,        // Last tick exceeded period — still scheduled but flagged
    SUSPENDED,      // Temporarily disabled (user or kernel request)
    FAULTED         // Unrecoverable error — will not be scheduled
};
```

**Transitions**:
```
UNINITIALIZED → IDLE            (init() completed successfully)
IDLE → READY                    (nextActivation <= now)
READY → RUNNING                 (Cruncher dispatches)
RUNNING → IDLE                  (tick() completes within period)
RUNNING → OVERRUN               (tick() exceeds period)
OVERRUN → READY                 (next activation due)
IDLE/READY/OVERRUN → SUSPENDED  (user/kernel suspend request)
SUSPENDED → IDLE                (resume request)
ANY → FAULTED                   (unrecoverable error)
```

**Integration with existing `ITask`**: The state is stored in `ScheduleSlot`, not in `ITask` itself. `ITask` gains an optional `onSuspend()` / `onResume()` callback pair (default no-op) so tasks can react to suspension.

### 10.3 Timer & Deadline Infrastructure

**Current state**: `TaskTimer` tracks last/max/average duration. `SputterTime.h` provides `SputterMicros` and `MicrosecondSource`. Timer rollover is detected in `System::tick()`.

**Required changes**:

#### 10.3.1 DeadlineTracker

New lightweight struct for absolute deadline tracking per slot:

```cpp
struct DeadlineTracker {
    SputterMicros nextActivation;   // Absolute time of next run
    SputterMicros periodUs;         // Repetition period
    SputterMicros phaseOffsetUs;    // Initial phase offset from epoch
    
    void init(SputterMicros startTime) {
        nextActivation = startTime + phaseOffsetUs;
    }
    
    bool isDue(SputterMicros now) const {
        return now >= nextActivation;
    }
    
    void advance(SputterMicros now) {
        nextActivation += periodUs;
        // Skip missed periods
        while (nextActivation <= now) {
            nextActivation += periodUs;
        }
    }
    
    SputterMicros timeUntilNext(SputterMicros now) const {
        return (nextActivation > now) ? (nextActivation - now) : 0;
    }
};
```

#### 10.3.2 TaskTimer Extensions

Add to existing `TaskTimer`:

```cpp
SputterMicros overrunCount() const;     // Number of ticks that exceeded declared budget
SputterMicros deadlineMissCount() const; // Number of skipped periods
void recordOverrun();
void recordDeadlineMiss();
```

#### 10.3.3 Minimum Clock Resolution Validation

At build time, assert that the platform's `MicrosecondSource` resolution is sufficient:

```cpp
// In SystemBuilder::build():
SputterMicros t0 = clockSource();
SputterMicros t1 = clockSource();
// If t1 - t0 > kMinSchedulePeriodUs, warn that clock resolution may be insufficient
```

This is a runtime check (cannot be compile-time), logged as a diagnostic warning.

### 10.4 System::tick() Refactor

**Current implementation**: Flat loop over all tasks on a core, unconditional tick, timer instrumentation.

**New implementation**: `System::tick()` delegates to the Cruncher.

```cpp
static void tick(std::size_t coreId, SputterMicros now) {
    assert(s_built);
    if (coreId >= kCoreCount) return;
    
    // Rollover detection (unchanged)
    if (now < s_lastTime[coreId] && s_lastTime[coreId] != 0)
        s_errorLogger.log(ErrorCode::TIMER_ROLLOVER, now, 0.0f);
    s_lastTime[coreId] = now;
    
    // Kernel state gate
    if (s_kernelState == KernelState::SHUTDOWN ||
        s_kernelState == KernelState::UNCONFIGURED)
        return;
    
    // Phase 1: Cruncher dispatch
    auto result = s_crunchers[coreId].tick(now);
    
    // Phase 2: Gap utilization (SystemScheduler)
    if (result == CruncherResult::GAP_AVAILABLE) {
        SputterMicros gap = s_crunchers[coreId].timeToNextDeadline(now);
        if (gap >= kMinGapSliceUs) {
            s_systemScheduler.tick(now, gap);
        }
    }
}
```

**No legacy fallback**: `System::tick()` exclusively delegates to the Cruncher. All tasks must be registered as `IScheduledTask` via `core(N).addScheduledTask()` or as `IBackgroundTask` via `addBackgroundTask()`. Calling `tick()` with no registered scheduled tasks results in immediate gap-time yield to the SystemScheduler on every call.

### 10.5 ITask Hierarchy Updates

#### ITask base updates:

Add classification queries and lifecycle hooks to `ITask`:

```cpp
virtual bool isScheduled() const { return false; }
virtual bool isBackground() const { return false; }
virtual void onSuspend() {}     // Called when scheduler suspends this task
virtual void onResume() {}      // Called when scheduler resumes this task
```

Remove `isCritical()` and `isAsync()` from `ITask` — these are retired with `ICriticalTask` / `IAsyncTask`.

#### Deleted base classes:

- `ICriticalTask` — **deleted**. Replaced by `IScheduledTask` with priority 0 on Core 0.
- `IAsyncTask` — **deleted**. Replaced by `IScheduledTask` (for periodic work) or `IBackgroundTask` (for best-effort work).

#### New base classes:

**`IScheduledTask`** — in `include/sputteros/osal/tasks/IScheduledTask.h`:
```cpp
class IScheduledTask : public ITask {
public:
    virtual SputterMicros periodUs() const = 0;
    virtual SputterMicros declaredWcetUs() const { return 0; }
    virtual uint8_t schedulePriority() const { return 0xFF; }
    bool isScheduled() const final { return true; }
    bool isBackground() const final { return false; }
};
```

**`IBackgroundTask`** — in `include/sputteros/osal/tasks/IBackgroundTask.h`:
```cpp
class IBackgroundTask : public ITask {
public:
    virtual SputterMicros maxBudgetUs() const { return 1000; }
    bool isBackground() const final { return true; }
    bool isScheduled() const final { return false; }
};
```

### 10.6 SystemBuilder Updates

#### New builder API:

```cpp
// Scheduled task registration
CoreBuilder& addScheduledTask(IScheduledTask* task);

// Background task registration
SystemBuilder& addBackgroundTask(IBackgroundTask* task);
```

**Removed**: `CoreBuilder::addTask(ITask*)` and `CoreBuilder::addPeriodicTask()` are removed. All tasks must be registered through the typed APIs above.

#### Build-time validation additions:

1. **Period floor check**: `slot.periodUs >= kMinSchedulePeriodUs` for all slots.
2. **Utilization check**: `sum(wcet[i] / period[i]) < 1.0` per core (warn if > 0.8).
3. **Core affinity for IScheduledTask**: Specified at registration time via `core(N).addScheduledTask()`, not encoded in the type.
4. **No duplicate task registration**: Same `IScheduledTask*` cannot appear in multiple slots or on multiple cores.
5. **No plain ITask registration**: `addTask(ITask*)` is removed; build fails if called.
5. **Priority uniqueness**: Within a core, warn if two slots have the same priority (resolved by registration order).

#### Automatic slot assignment for kernel tasks:

In `build()`, after creating kernel tasks:

```
Core 0, Slot 0: ScheduledControlTask<Cfg>   period = kControlBudgetUs, priority = 0
Core 1, Slot 0: ScheduledCommsTask<Cfg>      period = kCommsBudgetUs,   priority = 0
Background[0]:  BackgroundDiagnosticsTask    budget = kDiagsBudgetUs
```

### 10.7 ConfigTraits & Validator Updates

Add SFINAE extractors (following existing pattern) for new optional config fields:

```cpp
template <typename Cfg, typename = void> struct CfgMaxSlotsPerCore
{ static constexpr std::size_t value = 32; };

template <typename Cfg, typename = void> struct CfgMaxBackgroundTasks
{ static constexpr std::size_t value = 16; };

template <typename Cfg, typename = void> struct CfgCommsBudgetUs
{ static constexpr SputterMicros value = 1000; };

template <typename Cfg, typename = void> struct CfgDiagsBudgetUs
{ static constexpr SputterMicros value = 10000; };

template <typename Cfg, typename = void> struct CfgMinGapSliceUs
{ static constexpr SputterMicros value = 10; };

template <typename Cfg, typename = void> struct CfgMinSchedulePeriodUs
{ static constexpr SputterMicros value = 10; };

template <typename Cfg, typename = void> struct CfgStrictWCET
{ static constexpr bool value = false; };
```

### 10.8 DiagnosticsTask Scheduler Awareness

**Current role**: Ticks every cycle on Core 1, kicks watchdog, scans task timers, profiles memory.

**New role**: Runs as a SystemScheduler background task (no fixed core). Enhanced to report:

- Per-slot utilization: `wcet / period` ratio.
- Per-core total utilization: `sum(wcet / period)`.
- Deadline miss counters per slot.
- Overrun counters per slot.
- Background task starvation: time since last background task execution.
- Gap utilization: percentage of gap time actually used by SystemScheduler.

**Change**: `DiagnosticsTask` gains a `setMonitoredSlots(ScheduleSlot*, size_t)` method (in addition to existing `setMonitoredTasks()`) to access Cruncher slot arrays for diagnostics.

---

## 11. Implementation Phases

### Phase 1: Groundwork (No behavioral change)

1. Add `KernelState` enum and `s_kernelState` to `System<Cfg>`.
2. Add `TaskState` enum (standalone header, not yet used by scheduling).
3. Add `DeadlineTracker` struct.
4. Extend `TaskTimer` with overrun/deadline-miss counters.
5. Add new `IScheduledTask` and `IBackgroundTask` base classes.
6. Add new ConfigTraits SFINAE extractors.
7. Add `isScheduled()`, `isBackground()`, `onSuspend()`, `onResume()` to `ITask`.
8. Delete `ICriticalTask` and `IAsyncTask`.
9. Port `ControlTask` → `ScheduledControlTask`, `CommsTask` → `ScheduledCommsTask`, `DiagnosticsTask` → `BackgroundDiagnosticsTask`.
10. Update `SystemBuilder` to remove `addTask(ITask*)`, add `addScheduledTask()`, `addBackgroundTask()`.
11. Port all example projects and tests to new task types.
12. Unit tests for all new types in isolation.

**This is a breaking change.** All existing tests will be rewritten to use the new task hierarchy.

### Phase 2: Cruncher Implementation

1. Implement `Cruncher<kMaxSlots>` with slot table, activation scan, dispatch loop.
2. Add `s_crunchers[kCoreCount]` to `System<Cfg>`.
3. Refactor `System::tick()` to delegate to Cruncher.
4. Build-time validation: period floor, utilization warning.
5. Unit tests: single-core scheduling, multi-rate, overrun detection, deadline miss handling.
6. System tests: full pipeline with ported kernel tasks.

### Phase 3: SystemScheduler Implementation

1. Implement `SystemScheduler<kMaxBg>` with background ring and round-robin dispatch.
2. Add `s_systemScheduler` to `System<Cfg>`.
3. Wire gap-time handoff from Cruncher to SystemScheduler in `System::tick()`.
4. Move `DiagnosticsTask` to background ring.
5. Add `SystemBuilder::addBackgroundTask()`.
6. Atomic running-flag for cross-core mutual exclusion.
7. Unit tests: round-robin fairness, time-boxing, starvation detection.
8. System tests: mixed scheduled + background workloads.

### Phase 4: Kernel State Machine Integration

1. Wire `KernelState` transitions into `System::init()`, `System::tick()`, `ControlTask::evaluateSafety()`.
2. Cruncher respects `KernelState` (e.g., only safety slot during ABORTING).
3. SystemScheduler respects `KernelState` (paused during SHUTDOWN).
4. Add `System::pause()`, `System::resume()`, `System::shutdown()` static methods.
5. Integration tests: state machine transitions under various failure scenarios.

### Phase 5: Diagnostics & Observability

1. Enhance `DiagnosticsTask` with scheduler metrics.
2. Telemetry output for slot utilization, deadline misses, gap usage.
3. Build-time utilization report in `BuildResult`.

---

## 12. Testing Strategy

### Unit Tests

| Test | Validates |
|------|-----------|
| `DeadlineTracker_Init` | Phase offset, initial `nextActivation` |
| `DeadlineTracker_Advance` | Period advancement, missed period skipping |
| `Cruncher_SingleSlot` | Single task dispatches at correct period |
| `Cruncher_MultiRate` | 100 Hz + 1 kHz tasks interleave correctly |
| `Cruncher_PriorityOrder` | Higher-priority task runs first when both are due |
| `Cruncher_Overrun` | Task exceeding period is flagged, next deadline still advances |
| `Cruncher_DeadlineMiss` | Missed periods are skipped, not queued |
| `Cruncher_GapYield` | Returns GAP_AVAILABLE when no slots are ready |
| `SystemScheduler_RoundRobin` | Background tasks cycle fairly |
| `SystemScheduler_TimeBox` | Respects gap budget, doesn't overrun into next deadline |
| `SystemScheduler_AtomicGuard` | Same task not dispatched concurrently |
| `KernelState_ValidTransitions` | All valid transitions succeed |
| `KernelState_InvalidTransitions` | Invalid transitions are rejected and logged |
| `KernelState_AbortDuringRun` | Safety failure transitions to ABORTING, only safety slot runs |
| `Legacy_AutoWrap` | ~~Plain `ITask*` registered via `addTask()` becomes Cruncher slot~~ **Removed** — replaced by `ScheduledControlTask_Registration` below |

| Test | Validates |
|------|-----------|
| `ScheduledControlTask_Registration` | `ScheduledControlTask` auto-placed as Slot 0 / Core 0 with correct period |

### System Tests

| Test | Validates |
|------|-----------|
| `FullPipeline_ScheduledTasks` | End-to-end: build → init → multi-rate tick → correct task ordering |
| `DualCore_AMP_Isolation` | Core 0 schedule unaffected by Core 1 overruns |
| `GapUtilization_BackgroundRuns` | Background tasks execute when gap is available |
| `SafetyAbort_SchedulerPause` | Safety failure halts non-critical slots, safety slot continues |
| `Lifecycle_PauseResume` | Suspend/resume preserves task state and deadline phase |

Each test uses a unique `Cfg` type to isolate `System<>` static state, per existing convention.
