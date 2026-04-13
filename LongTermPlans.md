# SputterOS Long-Term Plans

Feasibility assessment and roadmap for potential improvements to SputterOS.
Each section evaluates an idea against the project's constraints (zero heap,
deterministic, hardware-agnostic, C++17, ~90 KiB static library).

**Legend — Effort / Risk / Payoff**
- Effort: T = Trivial (<2h), S = Small (2–8h), M = Moderate (8–20h), L = Large (20–60h), XL = Very Large (>60h)
- Risk: ◯ = None, ◑ = Low, ● = Medium, ⬤ = High
- Payoff: ★ = Marginal, ★★ = Moderate, ★★★ = Significant, ★★★★ = Transformative

---

## Table of Contents

1. [Standard Library Elimination](#1-standard-library-elimination)
2. [Scheduling & Tick Improvements](#2-scheduling--tick-improvements)
3. [Architectural Refactors](#3-architectural-refactors)
4. [Build & Toolchain Improvements](#4-build--toolchain-improvements)
5. [Example Project Deduplication](#5-example-project-deduplication)
6. [Summary Matrix](#6-summary-matrix)

---

## 1. Standard Library Elimination

### 1.0 Motivation

SputterOS kernel code currently uses 13 unique C++ standard library headers.
While all are freestanding-safe or stack-only, eliminating them would:
- Enable compilation with `-ffreestanding` (no hosted C++ runtime)
- Remove hidden locale, exception, and ABI dependencies
- Allow porting to bare-metal toolchains without libc++ or libstdc++
- Reduce binary size on resource-constrained MCUs

### 1.1 Phase 1 — Trivial Replacements

#### `<algorithm>` → inline min/max  | Effort: T | Risk: ◯ | Payoff: ★

Only `std::min` and `std::max` are used, in `PIDController.cpp`:
```cpp
std::max(m_outputMin, std::min(m_outputMax, output));
```
Replace with a two-line template or ternary. Already done in `PerformanceFormatter.cpp`
which hand-rolls all formatting.

#### `<cctype>` → inline character tests  | Effort: T | Risk: ◯ | Payoff: ★

`CommandParser.h` uses `std::isdigit()` and `std::isspace()`. Replace with:
```cpp
constexpr bool isDigit(char c) { return c >= '0' && c <= '9'; }
constexpr bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
```
These are locale-independent, unlike the libc versions. This also fixes the
latent locale-sensitivity bug identified in the Project Analysis Report.

#### `<cassert>` → custom assert macro  | Effort: T | Risk: ◯ | Payoff: ★★

Replace `assert()` with `SPUTTEROS_ASSERT(expr)`:
```cpp
#ifdef NDEBUG
  #define SPUTTEROS_ASSERT(x) ((void)0)
#else
  #define SPUTTEROS_ASSERT(x) do { if (!(x)) SPUTTEROS_FAULT(__FILE__, __LINE__); } while(0)
#endif
```
The fault handler could log to `ErrorLogger` or invoke a platform-provided
panic function, giving better diagnostics than libc `assert()` which calls
`abort()`.

### 1.2 Phase 2 — Moderate Replacements

#### `<chrono>` → raw microsecond types  | Effort: S | Risk: ◑ | Payoff: ★★

`std::chrono::milliseconds` appears in 6 kernel headers as timeout parameters
for `IMutex::lock()`, `IMessageQueue::try_push/try_pop`, `MultiCoreSync`
barriers, and `LockFreeQueue`.

The kernel already defines `SputterMicros` (`uint64_t`). Replace all
`std::chrono::milliseconds` parameters with a `SputterMillis` typedef
(`uint32_t`). This is a mechanical API change — every call site passes a
literal (e.g., `std::chrono::milliseconds{2000}` → `2000`).

**Risk:** Breaks the `IMutex` and `IMessageQueue` interface signatures.
All example projects and tests must update. But the change is purely
syntactical and can be done in a single pass.

#### `<cstring>` → compiler builtins or thin wrappers  | Effort: S | Risk: ◑ | Payoff: ★

Used for `memcpy`, `memset`, `strlen`, `strncpy` in CLI, FrameEncoder,
ProtocolRouter, ResponseSerializer, TelemetryLogger.

On GCC/Clang, these are compiler builtins (`__builtin_memcpy`, etc.) and
don't actually require the header at `-O1` or above. Two options:
1. **Wrapper header** — `#define sput_memcpy __builtin_memcpy` with a
   fallback to a hand-rolled loop for non-GCC/Clang toolchains.
2. **Just remove the include** — test whether GCC/Clang emit correct code
   without the header (they do for builtins, not guaranteed for `strlen`).

Option 1 is the safer path. Binary output should be identical.

#### `<optional>` → aligned storage + placement new  | Effort: M | Risk: ● | Payoff: ★★

Used in `System.h` for three kernel tasks:
```cpp
inline static std::optional<ScheduledControlTask<Cfg>> s_controlTask{};
```

Replace with:
```cpp
alignas(ScheduledControlTask<Cfg>) inline static
    unsigned char s_controlTaskStorage[sizeof(ScheduledControlTask<Cfg>)];
inline static bool s_controlTaskAlive{false};
```

Construction via `new (s_controlTaskStorage) ScheduledControlTask<Cfg>(...)`.
Destruction via explicit destructor call. The `reset()` method already handles
`optional::reset()` — replace with `if (s_controlTaskAlive) { ptr->~T(); s_controlTaskAlive = false; }`.

**Risk:** Loss of `std::optional`'s implicit destructor call and exception
safety (no exceptions in this codebase, so this is fine). The PassKey idiom
(`KernelConstructTag`) complicates `std::optional::emplace` today — aligned
storage actually simplifies it because placement new doesn't require move
constructibility.

#### `<type_traits>` → hand-rolled traits  | Effort: M | Risk: ◑ | Payoff: ★★

Used in `ConfigTraits.h` and `System.h` for:
- `std::is_enum_v<T>` — replace with `__is_enum(T)` (GCC/Clang/MSVC builtin)
- `std::is_base_of_v<B, D>` — replace with `__is_base_of(B, D)` (universal builtin)
- `std::void_t<...>` — replace with:
  ```cpp
  template <typename...> using void_t = void;
  ```
- `std::conditional_t<B, T, F>` — replace with:
  ```cpp
  template <bool B, typename T, typename F> struct conditional { using type = T; };
  template <typename T, typename F> struct conditional<false, T, F> { using type = F; };
  ```

All four are small, well-understood reimplementations. The compiler builtins
for `is_enum` and `is_base_of` are supported by every major compiler since
~2012.

#### `<cstdio>` → hand-rolled formatting  | Effort: M | Risk: ◑ | Payoff: ★★

Only `std::snprintf` is used, in `LightweightStringBuilder.cpp`, for three
patterns: `"%d"`, `"%u"`, and `"%.Nf"`. `PerformanceFormatter.cpp` already
hand-rolls equivalent integer and float formatting without `<cstdio>`.

Refactor: extract `PerformanceFormatter`'s `appendU32`, `appendU64`,
`appendFloat` helpers into a shared internal utility and use them in
`LightweightStringBuilder`. This also eliminates the locale-sensitivity of
`snprintf` with `%f` format (the `setlocale(LC_NUMERIC)` issue from the
Project Analysis Report).

### 1.3 Phase 3 — Defer or Skip

#### `<cstdlib>` → hand-rolled strtol/strtof  | Effort: L | Risk: ⬤ | Payoff: ★

`CommandParser.h` uses `std::strtol` (integer parsing) and `std::strtof`
(float parsing). Integer parsing is straightforward to hand-roll. Float
parsing is not — IEEE 754 rounding, subnormals, infinity, NaN, and
scientific notation create a minefield of edge cases.

**Options:**
1. **Hand-roll integer only** — replace `strtol` with a simple
   digit-accumulation loop (handles all command IDs). Keep `strtof` from
   libc. Effort: S, Risk: ◯. Partial win.
2. **Port musl's `strtof`** — musl's implementation is ~200 lines,
   battle-tested, permissively licensed (MIT). Effort: M, Risk: ◑.
3. **Full hand-roll** — not recommended. The risk of silent float
   misparsing on edge cases outweighs the benefit.

Recommendation: option 1 now (eliminate `strtol`), option 2 later if full
freestanding is required.

#### `<atomic>` → compiler intrinsics  | Effort: XL | Risk: ⬤ | Payoff: ★

Used pervasively in `LockFreeQueue`, `MultiCoreSync`, `WatchdogSync`,
`InterlockManager`, and `ErrorLogger` with `memory_order_acquire`,
`memory_order_release`, and `memory_order_relaxed`.

Replacing `std::atomic` with compiler intrinsics requires:
- GCC/Clang: `__atomic_load_n`, `__atomic_store_n`, `__atomic_compare_exchange_n`
- MSVC: `InterlockedCompareExchange`, `MemoryBarrier`, `_ReadWriteBarrier`
- Platform-specific memory ordering for ARM (DMB/DSB), x86 (mfence/sfence)

**This is the highest-risk change possible in the kernel.** A subtle memory
ordering bug in the lock-free queue or barrier would produce intermittent
data corruption or deadlocks that are extremely difficult to diagnose.
`std::atomic` is the correct abstraction — it compiles to the right
instructions on every platform.

**Recommendation:** Do not replace `<atomic>`. It is freestanding-compatible
in C++17 (`<atomic>` is in the freestanding subset per [library.requirements]),
works on bare-metal ARM Cortex-M, and carries zero runtime overhead beyond
the instructions themselves.

#### `<cstdint>` / `<cstddef>` / `<limits>`  | Skip

These are freestanding headers by definition (C++ standard §20.5.1.3). They
work on every conforming C++17 implementation, including `-ffreestanding`.
No replacement needed or beneficial.

### 1.4 Stdlib Elimination Summary

| Header | Phase | Effort | Risk | Approach |
|--------|-------|--------|------|----------|
| `<algorithm>` | 1 | T | ◯ | Inline `min`/`max` template |
| `<cctype>` | 1 | T | ◯ | Inline `constexpr` helpers |
| `<cassert>` | 1 | T | ◯ | `SPUTTEROS_ASSERT` macro |
| `<chrono>` | 2 | S | ◑ | `SputterMillis` typedef |
| `<cstring>` | 2 | S | ◑ | Compiler builtins / wrapper header |
| `<optional>` | 2 | M | ● | Aligned storage + placement new |
| `<type_traits>` | 2 | M | ◑ | Hand-rolled + compiler builtins |
| `<cstdio>` | 2 | M | ◑ | Reuse PerformanceFormatter helpers |
| `<cstdlib>` | 3 | L | ⬤ | Hand-roll strtol; port musl strtof |
| `<atomic>` | — | — | — | **Keep** (freestanding, correct by design) |
| `<cstdint>` | — | — | — | **Keep** (freestanding by spec) |
| `<cstddef>` | — | — | — | **Keep** (freestanding by spec) |
| `<limits>` | — | — | — | **Keep** (freestanding by spec) |

**After Phases 1–2:** Kernel depends only on `<atomic>`, `<cstdint>`,
`<cstddef>`, `<limits>`, and optionally `<cstdlib>` (for `strtof`). All of
these are freestanding or trivially replaced. The kernel can compile with
`-ffreestanding` on GCC/Clang.

---

## 2. Scheduling & Tick Improvements

### 2.1 Background Task Dispatcher (Phase 3 TODO)  | Effort: M | Risk: ● | Payoff: ★★★

**Current state:** `BackgroundDiagnosticsTask` is pinned to the core task
list as a "fake scheduled task" (see `SystemBuilder.h` lines 449, 456). All
tasks on a core are ticked every iteration regardless of whether they have
work to do.

**Proposed change:** Introduce a `BackgroundDispatcher` that runs after all
scheduled tasks complete, consuming remaining gap time:

```cpp
// Inside System<Cfg>::tick(), after the scheduled task loop:
SputterMicros elapsed = s_timer.nowMicros() - systemTimeMicros;
SputterMicros remaining = kControlBudgetUs - elapsed;

for (std::size_t i = 0; i < s_backgroundTaskCount && remaining > 0; ++i)
{
    IBackgroundTask *bg = s_backgroundTasks[s_bgRoundRobin];
    s_bgRoundRobin = (s_bgRoundRobin + 1) % s_backgroundTaskCount;

    bg->timer().start();
    bg->tick(s_timer.nowMicros());
    bg->timer().stop();

    remaining -= bg->timer().lastDuration();
}
```

Benefits:
- Background tasks run in idle time only, never displacing scheduled work
- Round-robin fairness across multiple background tasks
- Budget enforcement prevents background work from causing deadline misses
- `BackgroundDiagnosticsTask` moves out of the core task list (resolves the Phase 3 TODO)

This is the single highest-payoff scheduling change. It separates
deterministic scheduled work from best-effort background work in the actual
dispatch path, not just in the type system.

### 2.2 Period-Aware Skip Logic  | Effort: M | Risk: ● | Payoff: ★★

**Current state:** Every task's `tick()` is called every loop iteration.
Tasks that aren't due yet return immediately, but the call overhead (virtual
dispatch + timer start/stop) still exists. On a 100 Hz control loop with a
2 Hz reporting task, the reporting task is called 50× for every 1× it runs.

**Proposed change:** The kernel checks `(now - task.lastActivation) >= task.periodUs()`
before calling `tick()`:

```cpp
for (std::size_t t = 0; t < s_cores[coreId].taskCount; ++t)
{
    IScheduledTask *tsk = static_cast<IScheduledTask*>(s_cores[coreId].tasks[t]);
    if ((now - tsk->lastActivation()) < tsk->periodUs())
        continue;  // not due yet — skip entirely

    tsk->timer().start();
    tsk->tick(now);
    tsk->timer().stop();
    tsk->setLastActivation(now);
    busyAccum += tsk->timer().lastDuration();
}
```

Benefits:
- Eliminates virtual dispatch overhead for non-due tasks
- Timer metrics only record actual work, not no-op returns
- Tasks no longer need internal period tracking (kernel handles it)

Costs:
- `IScheduledTask` gains `lastActivation()` / `setLastActivation()` state
- Tasks that currently do partial work on non-due ticks would need redesign
- The kernel is no longer fully oblivious to task timing — this is a
  philosophical shift from "task autonomy" to "kernel-managed scheduling"

**Recommendation:** Implement as opt-in. Add a `bool kernelScheduled() const`
virtual (default `false`) to `IScheduledTask`. Tasks that return `true` get
kernel-managed period enforcement; others retain the current behavior.

### 2.3 Priority-Ordered Dispatch  | Effort: L | Risk: ● | Payoff: ★★

**Current state:** Tasks tick in registration order. `IScheduledTask` has a
`schedulePriority()` virtual (default `0xFF` meaning RMS auto-assign), but
it's unused by the flat-loop scheduler.

**Proposed change:** During `SystemBuilder::build()`, sort the per-core task
list by priority (lower value = higher priority). If `schedulePriority()`
returns `0xFF`, auto-assign using Rate Monotonic Scheduling (shorter period
= higher priority):

```cpp
// In build(), after all tasks registered:
for (std::size_t c = 0; c < kCoreCount; ++c)
{
    std::sort(s_cores[c].tasks, s_cores[c].tasks + s_cores[c].taskCount,
        [](ITask* a, ITask* b) {
            auto sa = static_cast<IScheduledTask*>(a);
            auto sb = static_cast<IScheduledTask*>(b);
            uint8_t pa = sa->schedulePriority();
            uint8_t pb = sb->schedulePriority();
            if (pa == 0xFF) pa = /* RMS: shorter period → lower value */;
            if (pb == 0xFF) pb = /* RMS: shorter period → lower value */;
            return pa < pb;
        });
}
```

This gives Rate Monotonic priority ordering for free ($2.3 combined with $2.2
gives a classic fixed-priority cooperative scheduler). No new fields needed —
`schedulePriority()` already exists in the interface.

**Note:** `std::sort` requires `<algorithm>` and may allocate. Use an
insertion sort (O(n²) is fine for n ≤ 32 tasks per core) to stay heap-free.

### 2.4 Deadline Miss Detection & Recovery  | Effort: M | Risk: ◑ | Payoff: ★★

**Current state:** `BackgroundDiagnosticsTask` checks `TaskTimer` data for
overruns and logs them. The kernel itself doesn't react to deadline misses.

**Proposed change:** If a task's `timer().lastDuration()` exceeds
`declaredWcetUs()` (user-declared) or a kernel-computed deadline, the kernel
can:
1. Log to `ErrorLogger` with `DEADLINE_MISS` code (already partially done)
2. Skip lower-priority tasks for this tick (requires §2.3)
3. Call `IScheduledTask::onOverrun()` — a new optional virtual for
   task-specific recovery (e.g., drop a sample, reduce fidelity)

This is the foundation for soft-real-time guarantees. Combined with §2.2
and §2.3, it gives: fixed-priority dispatch + period enforcement +
overrun detection + degradation callbacks.

### 2.5 Tick Timestamping Consistency  | Effort: S | Risk: ◯ | Payoff: ★

**Current state:** `run()` calls `tick(coreId, s_timer.nowMicros())`. Each
task sees the tick-start timestamp. But `tick()` also reads `s_timer.nowMicros()`
again at the end for gap computation. If `nowMicros()` is expensive (hardware
timer read), this is two reads per tick.

**Proposed change:** Read the clock once at tick start and once at tick end.
Pass both to instrumentation. Minor efficiency gain, but more importantly
gives consistent "tick wall time" without clock drift between the two reads.

---

## 3. Architectural Refactors

### 3.1 Static SystemBuilder  | Effort: M | Risk: ◑ | Payoff: ★★

**Current state:** `SystemBuilder<Cfg>` is instantiated as a local variable.
Since `System<Cfg>` is a template singleton with all `inline static` storage,
the builder could also be static — eliminating the local variable and enabling
a call syntax like:

```cpp
SystemBuilder<Cfg>::setStream(&stream);
SystemBuilder<Cfg>::core(0).addScheduledTask(&task);
SystemBuilder<Cfg>::build();
```

Or, equivalently, use the existing fluent API on a temporary (already supported):
```cpp
SystemBuilder<Cfg>(&app, monitors.data(), monitors.size())
    .setStream(&stream)
    .core(0).addScheduledTask(&task).done()
    .build();
```

**Trade-offs of static approach:**
- Pro: No local variable, globally accessible pre-`build()`
- Con: Longer call sites (`SystemBuilder<Cfg>::` prefix), test isolation
  requires reset, builder state lives in static memory permanently

**Recommendation:** The temporary-chain pattern already works today. A static
builder is feasible but adds test complexity for marginal syntactic gain.
Worth doing only if self-registration from global constructors becomes desired.

### 3.2 Eliminate `ICriticalTask` / `IAsyncTask` Shims  | Effort: S | Risk: ◯ | Payoff: ★

These interfaces are documented as "retired" / "compat shim" but still exist
in the codebase. Remove them and update any remaining references. Clean
deletion with no behavioral change.

### 3.3 Template-Free Kernel Core  | Effort: XL | Risk: ⬤ | Payoff: ★★

**Current state:** The entire kernel is templated on `Cfg` — `System<Cfg>`,
`SystemBuilder<Cfg>`, `ScheduledControlTask<Cfg>`, `ScheduledCommsTask<Cfg>`,
`LockFreeQueue<Cfg, N>`, `CommandParser<Cfg>`. Template parameters propagate
everywhere.

This forces all kernel code into headers (no `.cpp` compilation units for
templated code), increases compile times, and means each unique `Cfg` type
in tests instantiates a separate copy of the entire kernel.

**Possible approach:** Type-erase the `Cfg`-dependent parts:
- `Command` → fixed-size command packet (already `{ uint8_t id, uint8_t device, float value }`)
- `State` → `uint8_t` (the kernel doesn't interpret state values)
- `kCoreCount`, `kQueueCapacity` → runtime constructor parameters with
  compile-time bounds checking

This would let `System`, `ControlTask`, `CommsTask` be non-template classes
compiled once in a `.cpp` file, with `Cfg` validation happening only at
builder construction time.

**Risk:** Very high. This is a fundamental architecture change. The template
approach gives zero-cost abstraction and compile-time validation that
runtime parameters cannot match. The separate-instantiation property is a
feature for test isolation, not a bug.

**Recommendation:** Do not pursue unless compile times become a problem (they
are currently fast at ~90 KiB). The test isolation benefit alone justifies
the template design.

### 3.4 `System<Cfg>::reset()` as Public API  | Effort: S | Risk: ◑ | Payoff: ★★

**Current state:** `reset()` is private, accessible only via
`KernelTestAccess` friend struct. Tests need it; users might want it for
soft-restart scenarios (e.g., reconfigure after a fault without power cycle).

**Proposed change:** Make `reset()` public (or provide a `System<Cfg>::shutdown()`
public method that transitions to SHUTDOWN and clears state). Guard with a
state check — only callable from SHUTDOWN state.

### 3.5 Event-Driven Stop Conditions  | Effort: S | Risk: ◯ | Payoff: ★

**Current state:** Stop conditions are polling predicates evaluated every
tick. For event-driven exits (button press, watchdog timeout, external
signal), the predicate checks an atomic flag:
```cpp
builder.addStopCondition([]() { return g_stopFlag.load(); });
```

This works but wastes cycles polling per tick. An alternative: allow an
`IStopEvent` interface with a `notify()` method that directly sets an
internal stop flag, checked once per tick without evaluating all N predicates.

Low priority — the current polling approach has negligible overhead.

---

## 4. Build & Toolchain Improvements

### 4.1 `-ffreestanding` Compilation Mode  | Effort: M | Risk: ● | Payoff: ★★★

After stdlib elimination (§1 Phases 1–2), the kernel should compile with
`-ffreestanding -fno-exceptions -fno-rtti`. This verifies that no hidden
hosted dependencies remain and is the gate for bare-metal ARM Cortex-M
deployment.

Add a CMake option:
```cmake
option(SPUTTEROS_FREESTANDING "Compile kernel in freestanding mode" OFF)
if(SPUTTEROS_FREESTANDING)
    target_compile_options(SputterOS PRIVATE -ffreestanding -fno-exceptions -fno-rtti)
endif()
```

### 4.2 Size-Optimized Build Profile  | Effort: S | Risk: ◯ | Payoff: ★

Add `-Os` / `-Oz` build profiles for measuring the real binary footprint
on ARM targets. Currently only `-O2` and debug builds are profiled.

### 4.3 Static Analysis Integration  | Effort: S | Risk: ◯ | Payoff: ★★

Add `clang-tidy` with a `.clang-tidy` config enforcing:
- `modernize-*` (catch pre-C++17 idioms)
- `bugprone-*` (catch common mistakes)
- `cppcoreguidelines-*` (catch ownership and safety issues)
- `misc-no-recursion` (recursion is dangerous in embedded)
- Custom checks for heap-allocation patterns

### 4.4 Cross-Compilation CI  | Effort: L | Risk: ◑ | Payoff: ★★★

Add ARM Cortex-M cross-compilation targets to CI:
- `arm-none-eabi-gcc` with RP2040 (Pico SDK)
- `arm-none-eabi-gcc` with STM32 (HAL)

This validates the hardware-agnostic claim on real targets. Currently only
host-native (MSYS2/MinGW, Linux, macOS) builds are tested.

---

## 5. Example Project Deduplication

### 5.1 Common OSAL / HAL Extraction  | Effort: S | Risk: ◯ | Payoff: ★★

**Current state (from Project Analysis Report):**
- 5 OSAL files contain identical `platformGetTimeMicros()` (~145 lines duplicated)
- 4 HAL files contain identical `StdoutStreamReader` + `AlwaysSafeSafetyMonitor` (~120 lines)
- 2 `TimedMutexAdapter` files are byte-for-byte identical except namespace

**Proposed change:** Extract to `exampleProjects/common/`:
```
exampleProjects/common/
├── hal/
│   ├── TcpStreamServer.h          (already exists)
│   ├── StdoutStreamReader.h       (new — extract from 4 HAL files)
│   └── AlwaysSafeSafetyMonitor.h  (new — extract from 4 HAL files)
├── osal/
│   ├── HostTimeMicros.h           (new — extract from 5 OSAL files)
│   └── TimedMutexAdapter.h        (new — extract from 2 dual-core examples)
```

Each example project's `*HAL.h` / `*OSAL.h` would reduce to a one-line
include or be deleted entirely. Project-specific additions (e.g., SensorPoll's
`SimulatedADC`) stay in their own `hal_impl/`.

---

## 6. Summary Matrix

| ID | Item | Effort | Risk | Payoff | Depends On |
|----|------|--------|------|--------|------------|
| **1.1** | Stdlib Phase 1 (algorithm, cctype, cassert) | T | ◯ | ★ | — |
| **1.2** | Stdlib Phase 2 (chrono, cstring, optional, type_traits, cstdio) | M | ◑ | ★★ | 1.1 |
| **1.3** | Stdlib Phase 3 (cstdlib partial) | L | ⬤ | ★ | 1.2 |
| **2.1** | Background task dispatcher | M | ● | ★★★ | — |
| **2.2** | Period-aware skip logic | M | ● | ★★ | — |
| **2.3** | Priority-ordered dispatch | L | ● | ★★ | 2.2 |
| **2.4** | Deadline miss detection | M | ◑ | ★★ | 2.2, 2.3 |
| **2.5** | Tick timestamp consistency | S | ◯ | ★ | — |
| **3.1** | Static SystemBuilder | M | ◑ | ★ | — |
| **3.2** | Remove ICriticalTask/IAsyncTask shims | S | ◯ | ★ | — |
| **3.3** | Template-free kernel core | XL | ⬤ | ★★ | — |
| **3.4** | Public reset / soft-restart | S | ◑ | ★★ | — |
| **3.5** | Event-driven stop conditions | S | ◯ | ★ | — |
| **4.1** | Freestanding build mode | M | ● | ★★★ | 1.2 |
| **4.2** | Size-optimized build profile | S | ◯ | ★ | — |
| **4.3** | Static analysis (clang-tidy) | S | ◯ | ★★ | — |
| **4.4** | Cross-compilation CI | L | ◑ | ★★★ | 4.1 |
| **5.1** | Example project deduplication | S | ◯ | ★★ | — |

### Recommended Priority Order

1. **5.1** — Example deduplication (quick win, reduces maintenance burden)
2. **2.1** — Background dispatcher (resolves existing Phase 3 TODO, highest-payoff scheduling change)
3. **1.1** — Stdlib trivial replacements (< 2 hours, zero risk)
4. **3.2** — Remove retired interface shims (code hygiene)
5. **1.2** — Stdlib moderate replacements (enables §4.1)
6. **4.1** — Freestanding build mode (validates bare-metal readiness)
7. **2.2 + 2.3** — Period-aware dispatch + priority ordering (cooperative scheduler upgrade)
8. **4.3** — Static analysis integration (catch regressions)
9. **2.4** — Deadline miss detection (soft-real-time foundation)
10. **4.4** — Cross-compilation CI (proves hardware-agnostic claim)
