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

## Adding a New Example Project

1. Create `exampleProjects/<name>/` with the layered structure shown above.
2. Add a `CMakeLists.txt` that produces an executable target and registers it
   with `add_test()` under the `example_projects` label.
3. Add `add_subdirectory(<name>)` in `exampleProjects/CMakeLists.txt`.
4. The example will automatically appear in `make exampleProjects` and in CTest.
