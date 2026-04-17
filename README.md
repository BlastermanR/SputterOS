# SputterOS

A **deterministic, hardware-agnostic C++17 control framework** for high-speed, safety-critical nanofabrication and vacuum systems. Native Asymmetric Multiprocessing (AMP), lock-free polling architecture, and compile-time safety verification eliminate RTOS jitter and concurrency bugs, no bare-metal expertise required. The library is roughly (~70 KiB).

## What is SputterOS?

SputterOS is a **reusable control platform for nanofab systems**; sputtering, deposition, etching, analysis instruments, and any vacuum-based equipment. It provides deterministic control loop infrastructure, multi-tier safety interlocks, command routing, and process execution interfaces.

**SputterOS touches no hardware directly.** You supply HAL drivers and process logic; SputterOS orchestrates them and enforces safety through pure C++ interfaces. Because the entire framework is decoupled from hardware, you can validate your entire control system end-to-end on a desktop PC—no embedded hardware required during development.

### Why SputterOS?

**Microsecond Precision Without RTOS Jitter**  
Traditional RTOS schedulers introduce unpredictable context-switching overhead. SputterOS uses AMP (Asymmetric Multiprocessing) with a lock-free, polling-based architecture to deliver deterministic, tightly-bounded control loops, perfect for precision tuning and real-time process control.

**Compile-Time Safety, Not Runtime Surprises**  
SputterOS's `SystemBuilder<Cfg>` pattern shifts dangerous concurrency and configuration mistakes to compile time. Wrong core assignment? Type mismatch? Configuration constraint violation? The compiler catches it. No cryptic deadlocks at 3 AM.

**Zero Bare-Metal Firmware Skills Needed**  
Lab researchers and control engineers can write safe, microsecond-class control loops in standard C++17 without mastering bare-metal ISR handling, low-level atomics, and hardware-specific quirks. The framework handles the hard parts.

**Zero Hardware Complexity**  
Develop your entire control system on a standard PC without hardware dependencies. No embedded debugging nightmares, no hardware bring-up delays. Focus purely on process logic and safety rules; SputterOS handles the deterministic scheduling and multi-core coordination. Real hardware integration becomes a trivial HAL layer swap.

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
│   │  ├─ ScheduledControlTask<Cfg>                │   │
│   │  ├─ ScheduledCommsTask<Cfg>                  │   │
│   │  ├─ BackgroundDiagnosticsTask                │   │
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
│   IStream · ISputterDevice · ITask · ...             │
└──────────────────────────────────────────────────────┘
```

## Origins and Development

Welcome to the Hacker Fab, the collaborative open-source semiconductor fab. Everyone here is working together towards lowering the barrier to entry for semiconductor fabrication by making all the tools from scratch. End-to-end. From sand to NAND, and beyond.

The Hacker Fab is not just a place to learn about semiconductor fabrication. There is a whole internet for that. Everyone in the Hacker Fab is on a mission to build one centralized knowledge base for making nanofabricated devices from scratch.

Stay here if you want to give more than you take. The only thing that matters is how many people are able to recreate your work.

SputterOS was developed as part of **Carnegie Mellon University's Hacker Fab** initiative. Hacker Fab is CMU's rapid semiconductor prototyping and fabrication facility, providing students and researchers with access to custom nanofabrication infrastructure and advanced laboratory equipment.

SputterOS emerged from the need to control an RF sputtering magnetron on a custom RP2350-based platform. Rather than building a one-off control system, it was designed as a reusable, generic vacuum control library that could be deployed across multiple instruments and hardware platforms. The result is a production-ready framework suitable for any research group or company building vacuum-based equipment.

## Key Properties

- **Deterministic by Architecture** — Configurable control rate (default 100 Hz), lock-free polling, zero RTOS scheduler jitter, bounded safety response times
- **Asymmetric Multiprocessing (AMP) Native** — Automatic per-core task assignment, lock-free SPSC queue, acquire/release atomics, no global locks
- **Compile-Time Configuration Safety** — `SystemBuilder<Cfg>` validates topology, task types, and queue capacities at compile time; configuration errors become type errors
- **Reusable** across hardware platforms (RP2350, ESP32, STM32, ARM Cortex-M, custom boards)
- **Host-Testable** on a desktop PC — full unit and system test coverage with zero hardware dependencies
- **Zero-Heap Kernel** — all kernel storage is statically sized; no dynamic allocation in critical path
- **Safety-Ready** — multi-tier interlocks, microsecond-precision monitoring, deterministic abort handling

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
| `ScheduledControlTask<Cfg>` | `IScheduledTask` | Safety monitors → command drain → `IUserApplication::tick()` |
| `ScheduledCommsTask<Cfg>` | `IScheduledTask` | Serial byte ingestion → `CommandParser` → queue push |
| `BackgroundDiagnosticsTask` | `IBackgroundTask` | Watchdog kick, per-task `TaskTimer` budget enforcement, memory profiling |

In multi-core configurations, `SystemBuilder` automatically assigns `ScheduledControlTask` to Core 0 and `ScheduledCommsTask` to Core 1. In single-core mode, all tasks run on Core 0.

The user provides:

| You Provide | SputterOS Provides |
|---|---|
| `Cfg` struct (states, commands, capacities) | Compile-time `ConfigValidator` + kernel `System<Cfg>` singleton |
| `IUserApplication<Cfg>` (process logic) | `ScheduledControlTask<Cfg>` ticks your app every cycle |
| `ISafetyMonitor[]` adapters | Safety evaluation before every tick, abort on failure |
| HAL drivers (`IStream`, your domain devices) | `ScheduledCommsTask<Cfg>` for serial I/O + `BackgroundDiagnosticsTask` for health monitoring |
| Platform `main.cpp` wiring | `SystemBuilder` declarative topology API + multi-core sync (`LockFreeQueue`, watchdog, barriers) |

## Quick Start

1. **Add SputterOS to your project**
   ```bash
   git submodule add <repo-url> lib/SputterOS
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
  - Complete `templates/` directory — annotated `MyProjectConfig.h` and `main.cpp` for zero-hardware bring-up

- **Testing**
  - Host-native GoogleTest/CTest coverage for library logic, kernel tasks, OSAL utilities, and end-to-end pipelines
  - Dedicated system tests for lifecycle, interlocks, diagnostics, timer rollover, multi-rate scheduling, IO_PENDING coordination, and dual-core flows
  - Example projects that build and run as standalone smoke tests (multi-rate, IO_PENDING, lifecycle, heartbeat, pingpong)

## Documentation

- [**API Reference**](docs/APIReference.md) — complete public API: every class, method signature, parameter, and thread-safety note
- [System Architecture](docs/SystemArchitecture.md) — layers, components, data flow diagrams
- [Implementation Guide](docs/ImplementationGuide.md) — step-by-step integration walkthrough
- [Example Project Structure](docs/ExampleProjectStructure.md) — reference directory layout
- [ISR Methodology](docs/ISRMethodology.md) — interrupt-driven hardware within polling-based control
- [Multi-Core Implementation](docs/MultiCoreImplementation.md) — distributing SputterOS across CPU cores
- [Scheduling Design](docs/SchedulingDesign.md) — Dispatch algorithm, task hierarchy, rate-limiting, IO_PENDING, background tasks
- [Testing Guide](docs/TestingGuide.md) — build verification, unit tests, and formal test reports
- [Comment Style](docs/CommentStyle.md) — source code comment conventions

## Dependencies

No external runtime dependencies. GoogleTest is fetched automatically via
CMake `FetchContent` for host-native testing.

See [DEPENDENCIES.md](DEPENDENCIES.md) for the full list of development tools
(compilers, static analysis, documentation generators) with install commands
for Ubuntu, MSYS2, and macOS.

## Building and Testing

```bash
make test           # Run unit tests + system tests + example projects
make formalTest     # Run full suite and regenerate FormalTestResults.md
```

Individual targets:
```bash
make libOnly         # Library-only build (no tests)
make unitTest        # Build + run GoogleTest suite
make systemTest      # Build + run system-level integration tests
make exampleProjects # Build + run standalone example executables
make coverage        # Generate llvm-cov coverage report
make tidy            # Run clang-tidy static analysis
make docs            # Generate Doxygen API documentation (HTML + graphs)
make format          # Format all source files (clang-format)
```

| Suite | Purpose | Count |
|---|---|---|
| **Unit Tests** | Core logic, queues, kernel tasks, HAL/OSAL interfaces | See latest formal report |
| **System Tests** | End-to-end integration for lifecycle, interlocks, diagnostics, scheduling, and dual-core flows | See latest formal report |
| **Example Projects** | Standalone executable smoke tests in `exampleProjects/` | 5 executables |

All tests run on your host PC — **no embedded hardware or RTOS required**.

See [Testing Guide](docs/TestingGuide.md) and [Formal Test Results](FormalTestResults.md) for details.

Pull requests are validated automatically in GitHub Actions with formatting, unit tests, system tests, example projects, coverage generation, clang-tidy static analysis, and Doxygen documentation builds.

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
