# SputterOS Scheduling Evolution — Future Plans

Analysis of the current scheduling model, its limitations relative to AMP design
principles, and a prioritized roadmap for improvement.

**Status:** Planning  
**Baseline:** v0.5.0  
**Related documents:** [SchedulingDesign.md](../SchedulingDesign.md), [MultiCoreImplementation.md](../MultiCoreImplementation.md), [LongTermPlans.md](../../LongTermPlans.md)

---

## Table of Contents

1. [Current Scheduler Summary](#1-current-scheduler-summary)
2. [Identified Limitations](#2-identified-limitations)
3. [Scheduling Algorithm Survey](#3-scheduling-algorithm-survey)
4. [Recommended Evolution Path](#4-recommended-evolution-path)
5. [Phase Breakdown](#5-phase-breakdown)
6. [Effort and Risk Matrix](#6-effort-and-risk-matrix)

---

## 1. Current Scheduler Summary

SputterOS uses a **cooperative flat-loop scheduler**. Each core runs a `while` loop that
calls every registered task unconditionally on every iteration. There is no scheduler
decision logic at runtime.

### 1.1 The Tick Loop

```
while (running):
    // Phase 1 — all IScheduledTasks on this core, in registration order
    for each task in CoreData::tasks[]:
        timer.start()
        task.tick(now)      ← always called, regardless of period
        timer.stop()

    // Phase 2 — background ring (one designated core only)
    if (coreId == s_backgroundCoreId):
        round-robin dispatch from s_backgroundTasks[]
        subject to gap budget cap (kControlBudgetUs - phase1 cost)
```

### 1.2 Rate-Limiting Is Task-Internal

The kernel never skips a task. Each `IScheduledTask` self-rate-limits:

```cpp
void tick(SputterMicros now) override {
    if ((now - m_lastTick) < kPeriodUs) return;  // not due — ~5 ns exit
    m_lastTick = now;
    // do work
}
```

### 1.3 Background Dispatch Core Selection

A single `s_backgroundCoreId` field determines which core runs Phase 2. Set by
`SystemBuilder::build()` using the rule: **last active core wins**.

| Configuration | Background Core | Phase 1 on Core 0 | Phase 2 on Core 0 |
|---|---|---|---|
| Single-core | Core 0 | ✓ | ✓ |
| Dual-core FLAT_LOOP/FLAT_LOOP | Core 1 | ✓ | ✗ |
| Dual-core FLAT_LOOP/CRUNCH | Core 0 | ✓ | ✓ |

### 1.4 CRUNCH Mode

When `core(N).setCrunchTask()` is used, that core never calls `tick()`. It enters
`CrunchDispatcher::runLoop()` — a bare `while(active) crunch(now)` loop with no
Phase 1 or Phase 2. Safety abort is wired via `s_safetyAbort` atomic.

---

## 2. Identified Limitations

### 2.1 Background Tasks Pinned to One Core

The primary limitation: `s_backgroundCoreId` means background work is concentrated
on a single core. In a standard dual-core FLAT_LOOP setup, **Core 0's gap time is
completely idle** even when background tasks are backed up on Core 1.

This is inconsistent with AMP (Asymmetric Multi-Processing) design principles, where:
- Real-time tasks are statically pinned to specific cores (which SputterOS does correctly)
- Best-effort background work is opportunistically distributed across all available cores

### 2.2 No Priority Ordering

The flat loop executes tasks in registration order. A 2 Hz low-priority reporting task
and a 100 Hz high-priority safety sensor task share equal dispatch weight per iteration.
`IScheduledTask::schedulePriority()` exists as a field but is unused by the current
scheduler — it was added as a reserved extension point.

### 2.3 Background Budget Is Best-Effort Only

The gap budget cap (`kControlBudgetUs - busyAccum`) provides a ceiling but no floor.
If Phase 1 saturates the budget, only the first background task dispatches (the
"first always runs" guarantee). There is no minimum CPU share reservation for
background tasks — under load, all background tasks except the head of the ring
can be indefinitely starved.

### 2.4 BackgroundDiagnosticsTask Global State Assumption

`BackgroundDiagnosticsTask` is a single instance that profiles all cores' task timers and
kicks the global watchdog. This design assumes a single dispatch point. Any move toward
per-core background rings requires resolving whether the diagnostics task is replicated
per core or retains global shared state with atomic-guarded access.

---

## 3. Scheduling Algorithm Survey

The following algorithms were evaluated as potential replacements or evolutionary targets.

### 3.1 Cyclic Executive (Time-Triggered)

A static offline-computed table maps time slots to task invocations. The runtime is
a simple table index — zero decision-making.

| Property | Assessment |
|---|---|
| Determinism | Perfect — fully analyzable offline |
| Utilization | Fixed by slot assignment |
| Overload behavior | Missed slots logged; no cascading |
| Analyzability | Highest (DO-178C, AUTOSAR compatible) |
| SputterOS fit | Poor — hyperperiod explodes for non-harmonic periods |

The hyperperiod (LCM of all task periods) becomes impractically large when periods are
not harmonic ratios. A 163 µs crunch task alongside a 10 ms control task yields a
hyperperiod of ~5.5 seconds, requiring thousands of table slots. Rejected as primary
path; viable only if task periods are constrained to harmonic ratios by policy.

### 3.2 Cooperative Rate-Monotonic Scheduling (RMS)

Static priorities assigned by period: shorter period = higher priority. The scheduler
iterates tasks in priority order and runs the first ready task, repeating until no
ready tasks remain in the budget.

Schedulability bound (Liu & Layland):

$$U = \sum_{i=1}^{n} \frac{C_i}{T_i} \leq n(2^{1/n} - 1) \approx 0.693 \text{ as } n \to \infty$$

| Property | Assessment |
|---|---|
| Determinism | High — priority order is static |
| Utilization | ~69% guaranteed schedulable bound |
| Overload behavior | Low-priority tasks starve gracefully |
| Analyzability | High — standard real-time literature |
| SputterOS fit | Excellent — `schedulePriority()` field already exists |

The primary change from current behavior: sort `CoreData::tasks[]` by priority at
build time, modify the inner loop to skip non-ready tasks after the first miss.
This is a targeted, low-risk change.

### 3.3 Earliest Deadline First (EDF)

Assign each task an absolute deadline (`lastActivation + period`). Always dispatch the
task with the nearest deadline.

| Property | Assessment |
|---|---|
| Determinism | Medium |
| Utilization | Theoretically 100% (optimal under preemption) |
| Overload behavior | Cascading misses — difficult to certify |
| Analyzability | Medium — optimal but complex overload analysis |
| SputterOS fit | Poor — overload cascades violate safety-critical predictability |

Rejected for safety-critical control paths. EDF's 100% utilization bound applies only
under preemptive execution; under cooperative scheduling it degrades to less than RMS
guarantees while adding runtime complexity. Safety certification bodies (IEC 61508,
DO-178C) strongly prefer RMS or cyclic executive for this reason.

### 3.4 Two-Level: RMS + Constant Bandwidth Server (CBS)

Real-time tasks scheduled by RMS at the top level. Background tasks encapsulated in a
**CBS server** — a pseudo-scheduled-task with a fixed bandwidth reservation
`(Q_bg, T_bg)` that runs at the lowest priority and internally round-robins
background work.

```
Level 1 (RMS, priority order):
  ControlTask (10 ms) → CommsTask (10 ms) → UserTasks (by period)

Level 2 (CBS, lowest priority):
  BackgroundServer { budget = 500 µs, period = 10 ms }
    └─ round-robin: DiagnosticsTask → Logger → Monitor → ...
```

| Property | Assessment |
|---|---|
| Determinism | High |
| Utilization | RMS bound for real-time + guaranteed bg share |
| Overload behavior | Server absorbs overload; real-time tasks unaffected |
| Analyzability | High — CBS is formally analyzed in literature |
| SputterOS fit | Excellent — natural formalization of current gap-gating |

The CBS server replaces the ad-hoc `kControlBudgetUs` gap cap with a first-class
schedulable entity that has a provable minimum CPU share. Background tasks are
guaranteed to run regardless of Phase 1 load, not just when gap time happens to remain.

### 3.5 Partitioned RMS + Shared Background Pool (AMP Model)

Extends RMS + CBS to multi-core. Real-time tasks remain statically partitioned to
cores (current behavior). Each FLAT_LOOP core independently hosts its own CBS server
that drains from a **shared global background ring** using an atomic round-robin index.
CRUNCH cores have no CBS server.

```
Core 0 (FLAT_LOOP):                Core 1 (FLAT_LOOP):
  RMS: ControlTask                   RMS: CommsTask
  CBS: BackgroundServer ──┐          CBS: BackgroundServer ──┐
                          │                                  │
                          ▼                                  ▼
                   ┌─────────────────────────────────────────────┐
                   │  Shared background ring (atomic RR index)   │
                   │  DiagnosticsTask  Logger  Monitor  Telemetry │
                   └─────────────────────────────────────────────┘
```

| Property | Assessment |
|---|---|
| Determinism | High — per-core analysis independent |
| Utilization | Best multi-core utilization |
| Overload behavior | Per-core CBS isolation; one core's overload doesn't starve other's bg |
| Analyzability | High with per-core schedulability analysis |
| SputterOS fit | Best long-term fit — resolves all identified limitations |

Requires resolving `BackgroundDiagnosticsTask` ownership: either replicate one instance
per FLAT_LOOP core (each profiles its own core's tasks), or retain a single instance
protected by a `std::atomic` RR index (any core may dispatch it, first-come wins per tick).

---

## 4. Recommended Evolution Path

Three incremental phases, each independently shippable and non-breaking.

### Phase 1 — Activate `schedulePriority()` (Cooperative RMS)

**Target:** v0.6.0  
**Scope:** `SystemBuilder::build()` sorts `CoreData::tasks[]` by `schedulePriority()`;
`System::tick()` inner loop skips non-ready tasks after first miss.

This activates the already-designed extension point with minimal code change.
Existing users with default `schedulePriority()` (0xFF) retain identical behavior.

### Phase 2 — CBS Background Server

**Target:** v0.6.0 or v0.7.0  
**Scope:** Replace ad-hoc gap-gating with a `BackgroundServer` pseudo-task registered
at the lowest RMS priority. Carries `(kBgBudgetUs, kBgPeriodUs)` config fields.
Guarantees minimum background CPU share independent of Phase 1 load.

### Phase 3 — Shared Background Pool (Full AMP)

**Target:** v0.7.0  
**Scope:** Remove `s_backgroundCoreId`. Replace with `std::atomic<std::size_t>`
round-robin cursor shared across all FLAT_LOOP cores. Each core's CBS server
drains from the same `s_backgroundTasks[]` ring via atomic fetch-add.
Resolve `BackgroundDiagnosticsTask` per-instance vs. shared-atomic design.

---

## 5. Phase Breakdown

### Phase 1 — Priority Sorting

**Files changed:**

| File | Change |
|---|---|
| `SystemBuilder.h` | Sort `m_cores[c].tasks[]` by `schedulePriority()` after all tasks are added in `build()` |
| `System.h` | `tick()` inner loop: after first non-ready task, continue scan rather than unconditionally calling all |
| `IScheduledTask.h` | No change — `schedulePriority()` already declared |

**New tests:** `test_PriorityOrdering.cpp` — verify dispatch order matches priority,
not registration order.

### Phase 2 — CBS Background Server

**New file:** `include/sputteros/kernel/BackgroundServer.h`

```cpp
class BackgroundServer : public IScheduledTask {
public:
    SputterMicros periodUs()         const override { return kBgPeriodUs; }
    uint8_t       schedulePriority() const override { return 0xFE; } // lowest real-time slot

    void tick(SputterMicros now) override {
        if ((now - m_lastTick) < kBgPeriodUs) return;
        m_lastTick  = now;
        m_budgetUsed = 0;

        for (std::size_t i = 0; i < s_backgroundTaskCount; ++i) {
            IBackgroundTask *bg = s_backgroundTasks[m_rrIndex];
            m_rrIndex = (m_rrIndex + 1) % s_backgroundTaskCount;
            if (bg) {
                if (i > 0 && m_budgetUsed + bg->maxBudgetUs() > kBgBudgetUs) break;
                bg->timer().start();
                bg->tick(now);
                bg->timer().stop();
                m_budgetUsed += bg->timer().lastDuration();
            }
        }
    }
};
```

**Files changed:** `SystemBuilder.h` — register `BackgroundServer` instead of setting
`s_backgroundCoreId`. Remove `s_backgroundCoreId` from `System.h`. Remove Phase 2
block from `System::tick()`.

### Phase 3 — Shared Atomic Pool

**Files changed:**

| File | Change |
|---|---|
| `System.h` | Replace `s_backgroundCoreId` + `s_bgRoundRobin` with `inline static std::atomic<std::size_t> s_bgRoundRobin{0}` |
| `BackgroundServer.h` | Use `s_bgRoundRobin.fetch_add(1, std::memory_order_relaxed) % count` instead of per-instance index |
| `SystemBuilder.h` | Register one `BackgroundServer` per FLAT_LOOP core; remove `s_backgroundCoreId` selection logic |
| `BackgroundDiagnosticsTask.h/cpp` | Evaluate: replicate per FLAT_LOOP core (preferred) vs. atomic-guarded singleton |

---

## 6. Effort and Risk Matrix

| Phase | Effort | Risk | Breaking change | Payoff |
|---|---|---|---|---|
| Phase 1 — RMS priority sort | S (2–8 h) | ◯ None | No — default priority 0xFF preserves current order | ★★ Moderate |
| Phase 2 — CBS server | M (8–20 h) | ◑ Low | No — internal dispatch change only | ★★★ Significant |
| Phase 3 — Shared atomic pool | M (8–20 h) | ● Medium | No API break; `BackgroundDiagnosticsTask` design decision required | ★★★★ Transformative |

Phase 1 and Phase 2 are safe to combine in a single version bump. Phase 3 is independent
and should ship separately to isolate the multi-core diagnostic task design decision.
