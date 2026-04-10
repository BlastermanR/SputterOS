# SputterOS

A hardware-agnostic C++17 static library for building deterministic, safe nanofab control systems (~90 KiB).

## What is SputterOS?

SputterOS is a reusable control platform for nanofab systems - sputtering, deposition, etching, analysis instruments, and any vacuum-based equipment. It provides the core infrastructure: a deterministic control loop, multi-tier safety interlocks, command routing, task scheduling, and process execution interfaces.

**SputterOS touches no hardware directly.** You supply the drivers, process logic, and OS bindings; SputterOS orchestrates them and enforces safety through pure C++ interfaces.

```
┌──────────────────────────────────────────────────────┐
│   Your Application                                   │
│   IUserApplication · HAL Drivers · OSAL Bindings     │
└─────────────────────────┬────────────────────────────┘
                          │  dependency injection
                          ▼
┌──────────────────────────────────────────────────────┐
│   SputterOS Core                                     │
│   ┌──────────────────────────────────────────────┐   │
│   │ System<Cfg>  (runtime singleton)             │   │
│   │  ├─ ControlTask<Cfg>    (ICriticalTask)      │   │
│   │  ├─ CommsTask<Cfg>      (IAsyncTask)         │   │
│   │  ├─ DiagnosticsTask     (IAsyncTask)         │   │
│   │  ├─ LockFreeQueue       (SPSC ring buffer)   │   │
│   │  ├─ WatchdogSync        (heartbeat monitor)  │   │
│   │  └─ MultiCoreSync       (lifecycle barriers) │   │
│   └──────────────────────────────────────────────┘   │
│   SystemBuilder<Cfg>  (configuration + validation)   │
│   InterlockManager · CLI · PID · ErrorLogger · ...   │
└─────────────────────────┬────────────────────────────┘
                          │  pure abstract interfaces
                          ▼
┌──────────────────────────────────────────────────────┐
│   HAL + OSAL Interfaces (you implement)              │
│   IStreamReader · ISputterDevice · ITask · ...       │
└──────────────────────────────────────────────────────┘
```

## Origins and Development

Welcome to the Hacker Fab, the collaborative open-source semiconductor fab. Everyone here is working together towards lowering the barrier to entry for semiconductor fabrication by making all the tools from scratch. End-to-end. From sand to NAND, and beyond.

The Hacker Fab is not just a place to learn about semiconductor fabrication. There is a whole internet for that. Everyone in the Hacker Fab is on a mission to build one centralized knowledge base for making nanofabricated devices from scratch.

Stay here if you want to give more than you take. The only thing that matters is how many people are able to recreate your work.

SputterOS was developed as part of **Carnegie Mellon University's Hacker Fab** initiative. Hacker Fab is CMU's rapid semiconductor prototyping and fabrication facility, providing students and researchers with access to custom nanofabrication infrastructure and advanced laboratory equipment.

SputterOS emerged from the need to control an RF sputtering magnetron on a custom RP2350-based platform. Rather than building a one-off control system, it was designed as a reusable, generic vacuum control library that could be deployed across multiple instruments and hardware platforms. The result is a production-ready framework suitable for any research group or company building vacuum-based equipment.

## Key Properties

- **Reusable** across hardware platforms (RP2350, ESP32, STM32, custom boards)
- **Host-testable** on a desktop PC — host-native unit and system tests with zero hardware dependencies
- **Deterministic** configurable control rate (default 100 Hz) with bounded safety response
- **Multi-core ready** lock-free SPSC queue with acquire/release atomics
- **Zero heap allocation** all kernel storage is statically sized from your `Cfg`

## Architecture Overview

SputterOS uses a **System/SystemBuilder** pattern:

1. **`SystemBuilder<Cfg>`** — Configuration-time: validates topology, creates kernel tasks, and populates the runtime singleton
2. **`System<Cfg>`** — Runtime: owns all infrastructure as `inline static` data members and exposes the tick/init API

```cpp
// 1. Configure
SystemBuilder<MyConfig> builder(&app, monitors.data(), monitors.size());
builder.setStream(&usbStream);
builder.setWatchdogKick(watchdogKickFn);
auto result = builder.build();   // creates ControlTask, CommsTask, DiagnosticsTask
if (!result) { /* result.error describes the failure */ }

// 2. Run
System<MyConfig>::init(0);
while (true) {
    System<MyConfig>::tick(0, SputterMicros(platformGetTimeMicros()));
}
```

Three kernel tasks are auto-assigned by `build()` based on task type:

| Task | Type | Responsibility |
|---|---|---|
| `ControlTask<Cfg>` | `ICriticalTask` | Safety monitors → command drain → `IUserApplication::tick()` |
| `CommsTask<Cfg>` | `IAsyncTask` | Serial byte ingestion → `CommandParser` → queue push |
| `DiagnosticsTask` | `IAsyncTask` | Watchdog kick, per-task `TaskTimer` budget enforcement, memory profiling |

In multi-core configurations, `SystemBuilder` automatically assigns `ICriticalTask` to Core 0 and `IAsyncTask` to Core 1. In single-core mode, all tasks run on Core 0.

The user provides:

| You Provide | SputterOS Provides |
|---|---|
| `Cfg` struct (states, commands, capacities) | Compile-time `ConfigValidator` |
| `IUserApplication<Cfg>` (process logic) | `ControlTask` calling your app each tick |
| `ISafetyMonitor[]` adapters | Safety evaluation before every tick |
| HAL drivers (`IStreamReader`, your domain devices) | 1 dummy stub for bring-up |
| Platform `main.cpp` wiring | `SystemBuilder` declarative topology API |

## Quick Start

1. **Add SputterOS to your project**
   ```bash
   git submodule add <repo-url> lib/SputterOS
   git submodule update --init --recursive
   ```

2. **Define your `Cfg` struct** — states, command IDs, queue capacity:
   ```cpp
   struct MyConfig {
       enum class State : uint8_t { IDLE = 0, ROUGHING, DEPOSITING, FAULT_SAFE_MODE };
       enum class CmdID : uint8_t { SET_STATE = 0, ABORT_PROCESS, SET_GAS_FLOW };
       struct Command { CmdID id; uint8_t targetDevice; float value; };
       static constexpr std::size_t kCoreCount       = 2;
       static constexpr std::size_t kQueueCapacity   = 16;
       static constexpr int         kMaxCommandsPerTick = 8;
       static constexpr uint8_t     kMaxValidCommandID  = 2;
   };
   ```

3. **Implement `IUserApplication<MyConfig>`** — your process engine:
   ```cpp
   class MyApp : public SputterOS::IUserApplication<MyConfig> {
       void init() override { /* set initial state */ }
       void tick(SputterMicros systemTimeMicros) override { /* advance process */ }
       void handleCommand(const MyConfig::Command& cmd) override { /* route commands */ }
       void forceSafeAbort() override { /* de-energize everything */ }
   };
   ```

4. **Wire with `SystemBuilder`** in `main.cpp` and run:
   ```cpp
   SputterOS::SystemBuilder<MyConfig> builder(&app, monitors.data(), monitors.size());
   builder.setStream(&usbStream);
   builder.build();
   SputterOS::System<MyConfig>::init(0);
   while (true) { SputterOS::System<MyConfig>::tick(0, SputterMicros(platformGetTimeMicros())); }
   ```

See the [Implementation Guide](docs/ImplementationGuide.md) for the full walkthrough.

## Features

- **Deterministic Control Loop**
  - Configurable cycle rate (default 100 Hz / 10 ms budget)
  - Per-task execution timing via `TaskTimer`; `DiagnosticsTask` enforces budgets
  - Timer rollover detection with automatic fault logging

- **Multi-Tier Safety**
  - Tier 0: ISR-level fast faults (sub-μs) via `executeFastFault()` in HAL drivers
  - Tier 1: Kernel safety — `ISafetyMonitor[]` evaluated every tick, triggers `forceSafeAbort()`
  - Tier 2: User interlocks — `InterlockManager` with `IInterlockCondition` / `IFaultResponse`

- **ISR-Agnostic Polling**
  - Control loop never blocks on interrupts
  - ISRs latch state into atomics inside your HAL drivers; polling reads on next tick

- **System/SystemBuilder Pattern**
  - Declarative topology; zero manual kernel task wiring
  - `SystemBuilder` auto-assigns kernel tasks to cores based on task type
  - `KernelConstructTag` PassKey prevents direct construction of kernel tasks

- **Inter-Core Communication**
  - Lock-free SPSC command queue (`LockFreeQueue<Cfg, N>`) with atomic acquire/release
  - `MultiCoreSync<N>` lifecycle barriers; `WatchdogSync<N>` heartbeat monitor

- **Command & Telemetry**
  - ASCII command protocol with `CLI<Cfg>` (ACK/NACK responses)
  - `TelemetryLogger` — task-tagged, verbosity-filtered live telemetry
  - `ErrorLogger` — ISR-safe 32-entry circular ring buffer with 8 error codes

- **Utilities**
  - PID controller (discrete, anti-windup, chrono timestamps)
  - `MemoryProfiler` — heap/stack high-water mark tracking
  - 1 dummy HAL stub (`DummyStreamReader`) — start development with no hardware

- **Testing**
  - Host-native GoogleTest/CTest coverage for library logic, kernel tasks, OSAL utilities, and end-to-end pipelines
  - Dedicated system tests for lifecycle, interlocks, diagnostics, timer rollover, and dual-core flows
  - Example projects that build and run as standalone smoke tests

## Documentation

- [System Architecture](docs/SystemArchitecture.md) — layers, components, data flow diagrams
- [Implementation Guide](docs/ImplementationGuide.md) — step-by-step integration walkthrough
- [Example Project Structure](docs/ExampleProjectStructure.md) — reference directory layout
- [ISR Methodology](docs/ISRMethodology.md) — interrupt-driven hardware within polling-based control
- [Multi-Core Implementation](docs/MultiCoreImplementation.md) — distributing SputterOS across CPU cores
- [Testing Guide](docs/TestingGuide.md) — build verification, unit tests, and formal test reports
- [Comment Style](docs/CommentStyle.md) — source code comment conventions

## Dependencies

- **[nholthaus/units](https://github.com/nholthaus/units)** — header-only C++14 type-safe physical units (torr, sccm, watts). Configured with `DISABLE_IOSTREAM=ON` and `DISABLE_PREDEFINED_UNITS=ON` for embedded use.

```bash
git clone --recurse-submodules <sputteros-repo-url>
# or, if already cloned:
git submodule update --init --recursive
```

## Building and Testing

```bash
make test           # Run unit tests + system tests + example projects
make formalTest     # Run full suite and regenerate FormalTestResults.md
```

Individual targets:
```bash
make libOnly       # LibraryOnly build (no tests)
make unitTest       # Build + run GoogleTest suite
make systemTest     # Build + run system-level integration tests
make exampleProjects # Build + run standalone example executables
make coverage       # Generate llvm-cov coverage report
```

| Suite | Purpose | Count |
|---|---|---|
| **Unit Tests** | Core logic, queues, kernel tasks, HAL/OSAL interfaces | See latest formal report |
| **System Tests** | End-to-end integration for lifecycle, interlocks, diagnostics, and dual-core flows | See latest formal report |
| **Example Projects** | Standalone executable smoke tests in `exampleProjects/` | 2 executables |

All tests run on your host PC — **no embedded hardware or RTOS required**.

See [Testing Guide](docs/TestingGuide.md) and [Formal Test Results](FormalTestResults.md) for details.

## Project Status

**Stable**
- Core architecture finalized: `System<Cfg>` / `SystemBuilder<Cfg>` pattern
- HAL interfaces, OSAL interfaces, and kernel tasks defined and documented
- Host-native test suites and example projects passing
- Multi-core support validated on RP2350

**In Active Use**
- CMU HackerFab RP2350 sputtering system (integration in progress)

## Collaboration

SputterOS is actively developed as part of **Carnegie Mellon University's Hacker Fab**. Contributions are welcome:

- Read [Contributing Guide](.github/CONTRIBUTING.md) for setup, coding standards, and PR workflow
- Follow [Code of Conduct](.github/CODE_OF_CONDUCT.md) in all community interactions
- Review security posture and reporting limitations in [Security Policy](.github/SECURITY.md)
- Use SputterOS in your vacuum control project and provide feedback
- Implement HAL drivers for specialized equipment
- Share OSAL bindings (FreeRTOS, ThreadX, bare-metal) to help others
- Open an issue or contact the Hacker Fab maintainers

## License

See LICENSE file for terms and conditions.
