# Preemption Analysis for SputterOS Scheduling

An analysis of whether preemptive scheduling is a strength or weakness for the SputterOS AMP Cruncher design, including implementation complexity, performance trade-offs, and a final recommendation.

---

## Table of Contents

1. [Context](#context)
2. [What Preemption Means Here](#what-preemption-means-here)
3. [The Case for Preemption](#the-case-for-preemption)
4. [The Case Against Preemption](#the-case-against-preemption)
5. [Implementation Complexity](#implementation-complexity)
6. [Scheduling Theory: Utilization Bounds](#scheduling-theory-utilization-bounds)
7. [Practical Analysis: SputterOS Workloads](#practical-analysis-sputteros-workloads)
8. [Hybrid Approaches](#hybrid-approaches)
9. [Recommendation](#recommendation)
10. [Decision Matrix](#decision-matrix)

---

## 1. Context

The AMP Scheduling Design document proposes a **non-preemptive** Cruncher scheduler: once a task begins executing, it runs to completion before the Cruncher considers the next scheduling decision. This document examines whether adding preemption — the ability to interrupt a running task mid-execution to run a higher-priority task — would strengthen or weaken the design.

**Scope**: This analysis covers only the per-core Cruncher. The SystemScheduler (background tasks) is inherently cooperative and not considered for preemption.

---

## 2. What Preemption Means Here

### Non-Preemptive (Current Design)

```
Time →
Core 0: ┌──TaskA (100µs)──┐┌──TaskB (50µs)──┐┌──gap──┐
        ↑                   ↑                  ↑
        Cruncher dispatches A completes,       B completes,
        highest-priority    Cruncher picks B    yield to SystemScheduler
        READY task
```

If TaskB becomes READY while TaskA is running, TaskB **waits** until TaskA finishes. TaskB's response time = TaskA's remaining execution + TaskB's own execution.

### Preemptive

```
Time →
Core 0: ┌──TaskA──┐┌──TaskB (50µs)──┐┌──TaskA resumed──┐┌──gap──┐
        ↑          ↑                  ↑                   ↑
        Cruncher   TaskB becomes     TaskB completes,    TaskA completes
        dispatches READY, preempts A TaskA resumes
        TaskA
```

If TaskB becomes READY and has higher priority, TaskA is **interrupted** immediately. TaskB's response time = TaskB's own execution (no waiting). TaskA's completion is delayed by TaskB's duration.

---

## 3. The Case for Preemption

### 3.1 Reduced Worst-Case Response Time

The strongest theoretical argument. In a non-preemptive system, the worst-case response time for a high-priority task includes the longest execution time of any lower-priority task (the "blocking factor"):

$$R_i^{NP} = C_i + \max_{j: p_j < p_i}(C_j)$$

With preemption, the blocking term disappears for fixed-priority scheduling:

$$R_i^{P} = C_i + \sum_{j: p_j > p_i} \left\lceil \frac{R_i}{T_j} \right\rceil C_j$$

For a 10 µs PID loop sharing a core with a 1 ms communication handler, non-preemptive WCRT = 10 µs + 1000 µs = 1010 µs. Preemptive WCRT = 10 µs. The difference is dramatic.

### 3.2 Higher Utilization Bound

Rate-Monotonic Analysis for preemptive scheduling guarantees schedulability up to the Liu-Layland bound:

$$U_{lub}^P = n(2^{1/n} - 1) \xrightarrow{n \to \infty} \ln 2 \approx 0.693$$

Non-preemptive scheduling has a lower practical utilization bound because of the blocking term. For 2 tasks, preemptive RMA guarantees schedulability at ~83% utilization; non-preemptive drops to ~58% in the worst case (highly period-ratio dependent).

### 3.3 Temporal Isolation

With preemption, a misbehaving low-priority task cannot delay a high-priority safety task. This is a strong safety argument — `ScheduledControlTask` (safety evaluation) would always meet its deadline regardless of what user tasks do below it.

---

## 4. The Case Against Preemption

### 4.1 Stack Overhead

Each preemptible task needs its own stack for context preservation. On a Cortex-M33 (RP2350), a minimal saved context is 32 bytes (8 registers), but the interrupted task's stack frame includes all locals, temporaries, and call depth up to the preemption point.

| Approach | Stack Cost per Task |
|----------|-------------------|
| Non-preemptive (shared stack) | 0 extra — all tasks share the main stack |
| Preemptive (per-task stack) | 256–2048 bytes depending on task complexity |

For 32 slots × 512 bytes = **16 KiB** of extra stack RAM. On an RP2350 with 520 KiB SRAM this is manageable, but on smaller MCUs it is prohibitive. It also **violates the zero-heap constraint** unless all stacks are statically allocated (which requires compile-time sizing of every stack).

### 4.2 Context Switch Cost

Preemption requires saving and restoring CPU context at every preemption point. On Cortex-M33:

| Operation | Cost |
|-----------|------|
| Hardware exception stacking (8 regs) | ~12 cycles |
| PendSV handler entry + priority check | ~20-40 cycles |
| Software context save (remaining regs, FPU if used) | ~20-50 cycles |
| Stack switch | ~5 cycles |
| Software context restore | ~20-50 cycles |
| Exception return | ~12 cycles |
| **Total round-trip** | **~90-170 cycles ≈ 0.6–1.1 µs at 150 MHz** |

At 10 µs scheduling granularity, a 1 µs context switch consumes **10% of the budget**. For the fastest tasks, this is severe overhead.

### 4.3 Shared State Corruption

Non-preemptive tasks on the same core can safely share data without locking — they never run concurrently. Preemption breaks this guarantee. If a low-priority task is updating a multi-word struct and a high-priority task preempts to read it, the reader sees torn data.

**Fix**: Disable interrupts around critical sections, or use atomic operations. But this reintroduces the locking complexity that SputterOS deliberately avoids in the kernel.

### 4.4 Priority Inversion

Preemptive systems are susceptible to priority inversion: a high-priority task blocks on a resource held by a low-priority task, while a medium-priority task preempts the low-priority one, unboundedly delaying the high-priority task.

Classic solutions (Priority Inheritance Protocol, Priority Ceiling Protocol) add:
- Per-mutex priority tracking (~8 bytes per mutex)
- Dynamic priority adjustment logic
- Correctness-critical bookkeeping that is itself a source of bugs

SputterOS has no mutexes in the kernel. Introducing preemption creates the need for them.

### 4.5 Determinism and Debuggability

Non-preemptive execution is **fully reproducible** given the same inputs and timing. Task A always runs to completion once started. This makes:
- **Timing analysis** trivial: WCRT = WCET + max blocking.
- **Debugging** straightforward: no interrupted stack frames, no reentrancy bugs.
- **Testing** reliable: host-based tests don't need to simulate interrupt timing.

Preemption introduces **non-deterministic interleaving**. The same sequence of task activations can produce different execution orders depending on exact timing, making bugs timing-dependent and hard to reproduce.

### 4.6 Hardware Timer Consumption

Preemption requires a timer interrupt to trigger rescheduling. On Cortex-M33, this typically uses the SysTick timer or a hardware alarm. This:
- Consumes a scarce timer peripheral.
- Fires periodically even when no rescheduling is needed (polling overhead).
- Must be configured at the minimum scheduling period (10 µs = 100 kHz interrupt rate), generating significant ISR overhead.

---

## 5. Implementation Complexity

### 5.1 Non-Preemptive (Current Design)

| Component | Complexity | Notes |
|-----------|-----------|-------|
| Scheduler loop | ~50 lines | Scan slot table, dispatch, advance deadline |
| Context management | None | All tasks share the main stack frame |
| Synchronization | None on same core | Tasks don't overlap — no locking needed |
| Stack sizing | Trivial | Single stack, sized for deepest call chain |
| Testing | Host-testable | No interrupt simulation needed |
| **Total new kernel code** | **~200-300 lines** | Cruncher + DeadlineTracker + ScheduleSlot |

### 5.2 Preemptive

| Component | Complexity | Notes |
|-----------|-----------|-------|
| Scheduler loop | ~100 lines | Same as above + preemption check |
| Context switch routine | ~50-80 lines (ASM) | Architecture-specific, must be hand-written for each target (Cortex-M33, RISC-V, x86 for tests) |
| Per-task stack allocation | ~50 lines + config | Static arrays, user must size each stack |
| Stack overflow detection | ~30 lines | Canary words or MPU guard pages |
| Priority inheritance | ~100-150 lines | If any shared resources exist between tasks |
| Timer ISR setup | ~30 lines per platform | PendSV + SysTick configuration |
| Critical section API | ~40 lines | `disablePreemption()` / `enablePreemption()` |
| Host test harness | ~200 lines | Simulated context switching for deterministic tests |
| **Total new kernel code** | **~600-800 lines + ASM** | Significantly more surface area |

### 5.3 Porting Burden

| Aspect | Non-Preemptive | Preemptive |
|--------|----------------|------------|
| New architecture port | Zero platform code | Context switch ASM + timer ISR per arch |
| RP2350 (Cortex-M33) | Nothing | PendSV handler + SysTick config |
| ESP32 (Xtensa/RISC-V) | Nothing | Different register set, different exception model |
| Host tests (x86/ARM64) | Nothing | `ucontext` or `setjmp/longjmp` simulation |

SputterOS currently has **zero platform-specific code in the kernel** — all platform specifics are behind HAL/OSAL interfaces. Preemption would be the first piece of architecture-specific assembly in the kernel, breaking a fundamental design invariant.

---

## 6. Scheduling Theory: Utilization Bounds

### 6.1 Theoretical Comparison

For $n$ tasks with fixed priorities:

| Scheduling | Utilization Bound | Note |
|-----------|--------------------|------|
| Preemptive RMS | $n(2^{1/n} - 1)$ → 69.3% as $n$ → ∞ | Sufficient but not necessary |
| Non-preemptive RMS | Lower, depends on blocking | Approximately $U_{RMS} - B_{max}/T_{min}$ |
| Preemptive EDF | 100% (optimal) | But catastrophic under overload |
| Non-preemptive EDF | < 100%, blocking-dependent | |

### 6.2 Does the Bound Matter for SputterOS?

The utilization bound is relevant when the schedule is **tight** — many tasks competing for CPU time on one core. In practice:

**Typical SputterOS Core 0 workload:**
- `ScheduledControlTask`: 100 Hz, ~200 µs WCET → utilization = 2%
- Inner PID loop: 10 kHz, ~20 µs WCET → utilization = 20%
- Sensor polling: 1 kHz, ~30 µs WCET → utilization = 3%
- **Total: ~25%**

**Typical Core 1 workload:**
- `ScheduledCommsTask`: 1 kHz, ~100 µs WCET → utilization = 10%
- Protocol handler: 100 Hz, ~500 µs WCET → utilization = 5%
- **Total: ~15%**

At 25% utilization, both preemptive and non-preemptive scheduling are trivially feasible. The utilization bound advantage of preemption is **irrelevant** for the expected workloads.

The utilization argument becomes compelling only above ~50% per core, which would imply very aggressive task loads for an embedded control system.

---

## 7. Practical Analysis: SputterOS Workloads

### 7.1 The Blocking Problem in Context

The worst-case scenario for non-preemptive scheduling: a high-priority 10 µs task is blocked by a low-priority 1 ms task.

**Is this realistic?**

No. SputterOS enforces a design constraint: tasks must have WCET < period. A 1 ms WCET task must have period ≥ 1 ms (1 kHz). If the 10 µs task has period = 10 µs (100 kHz), the maximum blocking is 1 ms — unacceptable.

**But this is preventable at build time.** The Cruncher can enforce:

```
For each slot i on core C, with priority p_i:
  blocking_i = max WCET of all lower-priority slots on C
  assert(blocking_i + WCET_i <= period_i)
```

If this check fails, the build rejects the schedule. The user must either:
1. Move the long-running task to a different core.
2. Split the long task into sub-phases (cooperative chunking).
3. Increase the blocked task's period to accommodate the blocking.

This is a **design-time solution** to a problem that preemption solves at runtime. The design-time solution is simpler, cheaper, and fully deterministic.

### 7.2 Cooperative Chunking as an Alternative

If a task has a long WCET, it can be split into cooperative chunks:

```cpp
class LongTask : public IScheduledTask {
    enum class Phase { PHASE_A, PHASE_B, PHASE_C };
    Phase m_phase = Phase::PHASE_A;
    
    void tick(SputterMicros now) override {
        switch (m_phase) {
            case Phase::PHASE_A: doPartA(); m_phase = Phase::PHASE_B; break;
            case Phase::PHASE_B: doPartB(); m_phase = Phase::PHASE_C; break;
            case Phase::PHASE_C: doPartC(); m_phase = Phase::PHASE_A; break;
        }
    }
    
    SputterMicros periodUs() const override { return 333; } // 3× faster, 1/3 work each
};
```

This achieves the same effect as preemption (shorter blocking) without any infrastructure cost. It requires the user to structure their task as a state machine, but SputterOS's `IProcessState` already encourages this pattern.

---

## 8. Hybrid Approaches

If the analysis is not fully satisfying in either direction, there are middle-ground options:

### 8.1 ISR-Level Preemption Only (Interrupt-Triggered Safety Override)

The Cruncher remains non-preemptive for task-to-task scheduling, but a **hardware timer ISR** can preempt everything to run the safety-critical path:

```
Normal flow:    ┌──TaskA (slot 1)──┐┌──TaskB (slot 2)──┐
                                    ↑
Safety ISR:                         │ Timer fires at kControlBudgetUs
                                    │ ISR calls evaluateSafety()
                                    │ If unsafe → forceSafeAbort() from ISR context
                                    ↓
                ┌──TaskA──┐[ISR]┌──TaskA resumed──┐
```

This gives sub-µs safety response time without full preemptive scheduling. The ISR does only `evaluateSafety()` (~10 µs) — no context switch, no stack swap, no priority inheritance.

**Complexity**: ~50 lines (timer ISR setup + safety eval). Platform-specific timer configuration but no context switch ASM.

### 8.2 Deferred Preemption (Cooperation Points)

Tasks voluntarily yield at known-safe points by calling a `Cruncher::yield()` function that checks if a higher-priority task is ready:

```cpp
void tick(SputterMicros now) override {
    doExpensivePhaseA();
    Cruncher::yield();      // → if higher-prio task is READY, run it first, then return
    doExpensivePhaseB();
    Cruncher::yield();
    doExpensivePhaseC();
}
```

This is a hybrid: the scheduler is non-preemptive by default but tasks can opt into cooperative preemption points. No per-task stack needed (yield uses the current stack frame via a nested call). No timer ISR. No ASM.

**Complexity**: ~30 lines added to Cruncher (nested dispatch from yield point). Risk: if a task forgets to yield, it blocks — same as pure non-preemptive.

### 8.3 Two-Level: Preemptive Between Priority Bands, Non-Preemptive Within

Group tasks into 2-3 priority bands. Tasks within the same band are non-preemptive. Higher bands preempt lower ones.

```
Band 0 (safety):   ScheduledControlTask — can preempt anything
Band 1 (control):  PID loops, sensor polling — non-preemptive among themselves
Band 2 (comms):    CommsTask, protocol handlers — non-preemptive, preempted by Band 0-1
```

This limits context switch overhead to band transitions (rare) while protecting safety tasks.

**Complexity**: Moderate — 2-3 priority levels with per-band stacks. ~300 lines + minimal ASM for band switching.

---

## 9. Recommendation

### Primary Recommendation: **Non-Preemptive with Build-Time Blocking Validation**

For SputterOS, the non-preemptive Cruncher design is the correct choice. The reasoning:

1. **Workload utilization is low** (~25% per core). The preemptive utilization advantage is wasted.
2. **Zero extra RAM** — no per-task stacks, no stack overflow detection, no canary overhead.
3. **Zero platform-specific kernel code** — no context switch ASM, no timer ISR, no PendSV handler. The kernel remains pure portable C++17.
4. **The blocking problem is solvable at build time** — the builder validates that `max_blocking + WCET ≤ period` for every slot. Infeasible schedules are rejected before the system runs. This is *stronger* than preemption, which handles the problem at runtime (and can still fail under overload).
5. **Deterministic execution** — same inputs, same timing, same order. Testable on host without interrupt simulation.
6. **Complexity budget** — ~300 lines vs ~800 lines + ASM. Every line of context-switch code is a potential source of hard-to-diagnose bugs.

### Secondary Recommendation: **Add ISR-Level Safety Override (Hybrid 8.1) in Phase 4**

As a targeted enhancement, wire `evaluateSafety()` to a hardware timer ISR so that safety evaluation is guaranteed to run at the control period regardless of what any task is doing. This provides sub-µs safety response without full preemption.

This is compatible with the non-preemptive Cruncher — it's an orthogonal safety net, not a scheduler change.

### Not Recommended: Full Preemption

Full preemptive scheduling is **not recommended** for SputterOS because:
- It breaks the zero-platform-code-in-kernel invariant.
- It requires per-task stacks that conflict with the zero-heap, statically-sized memory model.
- Its benefits (higher utilization bound, reduced blocking) are not needed at the expected workload levels.
- It introduces shared-state corruption risks that require locking primitives the kernel deliberately avoids.

If a future application genuinely needs >60% per-core utilization with mixed-criticality tasks, the recommended path is to layer FreeRTOS beneath SputterOS (OSAL integration) rather than implementing preemption inside the kernel.

---

## 10. Decision Matrix

| Criterion | Weight | Non-Preemptive | Preemptive | Hybrid (ISR Safety) |
|-----------|--------|----------------|-----------|---------------------|
| Implementation complexity | 20% | ★★★★★ (~300 LOC) | ★★☆☆☆ (~800 LOC + ASM) | ★★★★☆ (~350 LOC) |
| RAM overhead | 15% | ★★★★★ (0 extra) | ★★☆☆☆ (16+ KiB stacks) | ★★★★★ (0 extra) |
| Worst-case response time | 20% | ★★★☆☆ (blocking term) | ★★★★★ (no blocking) | ★★★★☆ (safety path unblocked) |
| Determinism / testability | 15% | ★★★★★ (fully reproducible) | ★★★☆☆ (timing-dependent) | ★★★★★ (ISR is deterministic) |
| Portability | 10% | ★★★★★ (pure C++17) | ★★☆☆☆ (per-arch ASM) | ★★★★☆ (timer ISR per platform) |
| Safety guarantee | 15% | ★★★★☆ (build-time check) | ★★★★★ (runtime preempt) | ★★★★★ (ISR forces safety eval) |
| Debugging difficulty | 5% | ★★★★★ (sequential) | ★★☆☆☆ (interleaved stacks) | ★★★★★ (sequential + ISR) |
| **Weighted Score** | | **4.35** | **3.15** | **4.40** |

**Verdict**: Non-preemptive Cruncher + ISR-level safety override scores highest. Full preemption is reserved as an OSAL-layer option (e.g., FreeRTOS port) for workloads that genuinely exceed the non-preemptive utilization bound.
