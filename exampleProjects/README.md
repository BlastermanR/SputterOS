# SputterOS Example Projects

Standalone executable projects that exercise the full SputterOS runtime.
Each example constructs a complete kernel from real library components and
a thin platform layer, demonstrating how to wire `SystemBuilder`, inject
dependencies, and drive the `System` tick loop.

These examples also serve as smoke tests — each is registered with CTest
so `make exampleProjects` verifies they build and run successfully.

---

## Design Principles

### Platform-agnostic by construction

Every example project separates concern into four independent layers:

```
<name>/
├── config/         <Name>Config.h       — Cfg template parameter
├── osal_impl/      <Name>OSAL.h         — Time source, threading primitives
├── hal_impl/       <Name>HAL.h          — Device stubs / concrete drivers
├── include/        State machine, user task headers
└── src/
    └── main_osNative.cpp                — Platform entry point
```

Adding a Pico variant requires only a new `main_pico.cpp` and, if needed, a
new `<Name>Config_pico.h`.  The state machine, task implementations, and
`config/`, `hal_impl/`, `osal_impl/` headers are reused without modification.

---

## Build & Run

### Standalone

```sh
cmake -B exampleProjects/build -S exampleProjects -G Ninja
ninja -C exampleProjects/build
ctest --test-dir exampleProjects/build --output-on-failure
```

### Via Makefile (from `SputterOS/`)

```sh
make exampleProjects
```

### Filter by label

```sh
ctest --test-dir exampleProjects/build \
      --label-regex example_projects --output-on-failure
```

---

## Projects

### HeartBeat

**Purpose:** Demonstrate the three kernel tasks and a minimal custom task
in a single-core (`kCoreCount = 1`) main loop.

**Tasks running:**

| Core | Task | Role |
|------|------|------|
| 0 | `ControlTask<HeartbeatConfig>` | Safety loop — interlock evaluation, state machine tick |
| 0 | `CommsTask<HeartbeatConfig>` | CLI bridge — reads zero bytes, forwards telemetry |
| 0 | `DiagnosticsTask` | Health monitor — error log, memory profiler |
| 0 | `PulseTask` | Custom 4th task — prints "Pulse #N" every 500 ms |

**Expected output** (one line every ~500 ms, then program exits):

```
[<us>][Control] Pulse #1
[<us>][Control] Pulse #2
[<us>][Control] Pulse #3
[<us>][Control] Pulse #4
[<us>][Control] Pulse #5
```

---

### PingPong

**Purpose:** Demonstrate the full dual-core (`kCoreCount = 2`) kernel path —
two `std::thread` instances simulate Core 0 and Core 1, exercising
`MultiCoreSync<2>` startup/shutdown barriers and lock-free inter-core
counter passing via `std::atomic` release/acquire pairs.

**Project structure:**

```
pingpong/
├── config/         PingPongConfig.h     — kCoreCount = 2
├── osal_impl/      PingPongOSAL.h       — steady_clock time source
├── hal_impl/       PingPongHAL.h        — null stream, always-safe monitor
├── include/        PingPongStateMachine.h, SharedCounter.h,
│                   PingTask.h, PongTask.h
└── src/
    └── main_osNative.cpp               — dual-threaded platform entry point
```

**Tasks running:**

| Core | Task | Role |
|------|------|------|
| 0 | `ControlTask<PingPongConfig>` | Safety loop |
| 0 | `PingTask` | Increments shared counter, releases turn token to Core 1 |
| 1 | `CommsTask<PingPongConfig>` | CLI bridge |
| 1 | `DiagnosticsTask` | Health monitor |
| 1 | `PongTask` | Increments shared counter, returns turn token to Core 0 |

**Inter-core communication protocol:**

Three `inline std::atomic` variables in `SharedCounter.h` coordinate the exchange:

| Variable | Type | Purpose |
|----------|------|---------|
| `g_counter` | `atomic<uint32_t>` | Shared counter, advanced once per turn |
| `g_pingTurn` | `atomic<bool>` | Turn token — `true` = Core 0's turn, `false` = Core 1's turn |
| `g_running` | `atomic<bool>` | Shutdown flag, cleared by `PingTask` after `kMaxRounds` |

`g_counter` uses `memory_order_relaxed` for the `fetch_add`; the subsequent
`g_pingTurn` store/load using `memory_order_release`/`acquire` acts as the
visibility fence — no mutexes required.

**Expected output** (5 rounds, ~300 ms apart, then program exits):

```
[Core 0] Ping #1 -> counter = 1
[Core 1] Pong #1 -> counter = 2
[Core 0] Ping #2 -> counter = 3
[Core 1] Pong #2 -> counter = 4
[Core 0] Ping #3 -> counter = 5
[Core 1] Pong #3 -> counter = 6
[Core 0] Ping #4 -> counter = 7
[Core 1] Pong #4 -> counter = 8
[Core 0] Ping #5 -> counter = 9
```

---

### MultiRate

**Purpose:** Demonstrate the Cruncher's multi-rate dispatch — two scheduled
tasks at different frequencies sharing Core 0, with a background task filling
idle gap time via the SystemScheduler.

**Tasks running:**

| Core | Task | Type | Rate |
|------|------|------|------|
| 0 | `ScheduledControlTask` | Kernel (scheduled) | 100 Hz |
| 0 | `ScheduledCommsTask` | Kernel (scheduled) | 1 kHz |
| 0 | `FastSampleTask` | User (scheduled) | 100 Hz (10 ms) |
| 0 | `SlowReportTask` | User (scheduled) | 2 Hz (500 ms) |
| 0 | `BackgroundDiagnosticsTask` | Kernel (background) | Gap time |
| — | `IdleCounterTask` | User (background) | Gap time |

**Key concepts demonstrated:**

- **RMS priority ordering**: `FastSampleTask` (10 ms) gets higher Cruncher
  priority than `SlowReportTask` (500 ms) — shorter period = higher priority.
- **Data flow between rates**: `FastSampleTask` accumulates simulated sensor
  readings; `SlowReportTask` drains the accumulator every 500 ms.
- **Background gap utilisation**: `IdleCounterTask` runs via SystemScheduler
  whenever no scheduled slots are due, demonstrating that idle CPU time is
  reclaimed without affecting deterministic task deadlines.

**Expected output** (~3 seconds):

```
[<ts>][System] Report #1 | samples=1 accum=0
[<ts>][System] Report #2 | samples=34 accum=561
[<ts>][System] Report #3 | samples=66 accum=1584
[<ts>][System] Report #4 | samples=99 accum=2706
[<ts>][System] Report #5 | samples=132 accum=595
[<ts>][System] Report #6 | samples=165 accum=1584

--- MultiRate Summary ---
Fast samples:         ~170
Background dispatches: 0
```

> **Note:** Background dispatches will remain 0 until the SystemScheduler
> (Phase 3) is implemented.  Once gap-time dispatch is active, the
> `IdleCounterTask` will report ~2 500 dispatches per run.

---

### SensorPoll

**Purpose:** Demonstrate the `IO_PENDING` non-blocking IO pattern — a
scheduled task starts a simulated ADC conversion, yields immediately, and
polls for completion on subsequent ticks without blocking the scheduler.

**Tasks running:**

| Core | Task | Type | Rate |
|------|------|------|------|
| 0 | `ScheduledControlTask` | Kernel (scheduled) | 100 Hz |
| 0 | `ScheduledCommsTask` | Kernel (scheduled) | 1 kHz |
| 0 | `AdcPollTask` | User (scheduled) | 20 Hz (50 ms) |
| 0 | `BackgroundDiagnosticsTask` | Kernel (background) | Gap time |
| — | `SensorLogTask` | User (background) | Gap time |

**Key concepts demonstrated:**

- **IO_PENDING yield**: `AdcPollTask` calls `startConversion()` and returns.
  The Cruncher marks the slot as `IO_PENDING` (via `isIoPending()`).  On the
  next activation, the task re-checks `isConversionReady()`.
- **Fault detection**: If the ADC doesn't respond within `kMaxIoRetries`
  polls, the task logs a `CRITICAL` fault and resets — safety evaluation is
  never stalled.
- **Simulated hardware**: `SimulatedADC` models a 15 ms conversion delay
  using the platform clock, exercising the real timing relationships.
- **Background aggregation**: `SensorLogTask` runs in gap time, summarising
  accumulated readings without affecting the polling schedule.

**Expected output** (~3 seconds, 20 Hz polling):

```
[<ts>][ControlTask] ADC #1 = 0.000 mV
[<ts>][ControlTask] ADC #2 = 37.000 mV
[<ts>][ControlTask] ADC #3 = 74.000 mV
[<ts>][ControlTask] ADC #4 = 111.000 mV
[<ts>][ControlTask] ADC #5 = 148.000 mV
...
[<ts>][ControlTask] ADC #59 = 146.000 mV

--- SensorPoll Summary ---
ADC readings:  ~59
Last reading:  146.0 mV
```

> **Note:** `SensorLogTask` (background) will produce periodic log summaries
> once the SystemScheduler (Phase 3) is implemented.

---

### Lifecycle

**Purpose:** Demonstrate dual-core AMP scheduling with kernel state machine
observation.  `WorkerTask` on Core 0 and `MonitorTask` on Core 1 run at
different frequencies via independent Cruncher instances, with `MonitorTask`
reading `System<Cfg>::kernelState()` to track lifecycle transitions.

**Tasks running:**

| Core | Task | Type | Rate |
|------|------|------|------|
| 0 | `ScheduledControlTask` | Kernel (scheduled) | 100 Hz |
| 0 | `WorkerTask` | User (scheduled) | 5 Hz (200 ms) |
| 1 | `ScheduledCommsTask` | Kernel (scheduled) | 1 kHz |
| 1 | `MonitorTask` | User (scheduled) | 2 Hz (500 ms) |
| 1 | `BackgroundDiagnosticsTask` | Kernel (background) | Gap time |

**Key concepts demonstrated:**

- **Kernel state observation**: `MonitorTask` calls `System<Cfg>::kernelState()`
  each tick and logs the state name (`CONFIGURED`, `INITIALIZING`, `RUNNING`).
- **Dual-core AMP**: Each core has its own Cruncher with independently
  scheduled tasks.  Core 0's 5 Hz worker is unaffected by Core 1's 2 Hz
  monitor.
- **Lifecycle hooks**: `WorkerTask` implements `onSuspend()` and `onResume()`
  — these will activate once `System::pause()/resume()` are added in Phase 4.
- **Multi-core barriers**: `MultiCoreSync<2>` synchronises startup and
  shutdown, preventing Core 1 from ticking before Core 0 is ready.

**Expected output** (~3 seconds):

```
--- Lifecycle Example: Dual-Core AMP Scheduling ---
Kernel state after build(): 1
Kernel state after init():  3
Both cores running. Main loop for ~3 seconds...

[<ts>][CommsTask] [Core 1] State report #1: RUNNING
[<ts>][ControlTask] [Core 0] Worker tick #1
[<ts>][ControlTask] [Core 0] Worker tick #2
[<ts>][ControlTask] [Core 0] Worker tick #3
[<ts>][CommsTask] [Core 1] State report #2: RUNNING
...

--- Lifecycle Summary ---
Worker iterations (Core 0): ~15
Final kernel state:         3
```

---

## Adding a New Example Project

1. Create `exampleProjects/<name>/` with the layered structure shown above.
2. Add a `CMakeLists.txt` that produces an executable target and registers it
   with `add_test()` under the `example_projects` label.
3. Add `add_subdirectory(<name>)` in `exampleProjects/CMakeLists.txt`.
4. The example will automatically appear in `make exampleProjects` and in CTest.
