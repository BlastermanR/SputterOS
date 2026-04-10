# Example Project Structure

This document provides a complete reference layout for a typical user project integrating SputterOS as a library.

## Directory Layout

```
UserProject/
├── CMakeLists.txt                    # User's build config
├── pico_sdk_import.cmake             # User's SDK setup (if using Pico)
│
├── include/                          # User's Application Headers
│   ├── config/
│   │   ├── BoardConfig.h             # Pinouts, ADC calibration
│   │   ├── MyConfig.h                # Your ConfigTraits struct (replaces SystemConfig.h)
│   │   └── ProcessConfig.h           # Target thicknesses, custom timers
│   │
│   ├── osal_impl/                    # User's OS-specific code
│   │   ├── PicoHardwareFIFO.h
│   │   └── PicoSpinlock.h
│   │
│   └── hal_impl/                     # User's Hardware-specific code
       ├── PioUsbStreamReader.h
│       └── GPIO_RelayControl.h
│
├── src/                              # User's Application Source
│   ├── main.cpp                      # SystemBuilder wiring → System::init() → tick loop
│   ├── MyProcessApp.cpp              # Your IUserApplication<Cfg> implementation
│   ├── osal_impl/                    # Implementations of OSAL interfaces
│   │   ├── PicoHardwareFIFO.cpp
│   │   └── PicoSpinlock.cpp
│   │
│   └── hal_impl/                     # Implementations of HAL interfaces
       ├── PioUsbStreamReader.cpp
│       └── GPIO_RelayControl.cpp
│
├── tests/                            # Your unit tests
│   ├── CMakeLists.txt
│   ├── test_my_state_machine.cpp
│   └── test_hal_implementations.cpp
│
├── README.md                         # Your project documentation
├── .gitmodules                       # Git submodule configuration
│
└── lib/                              # Submodules Directory
    └── SputterOS/                    # >>> SPUTTEROS CORE REPOSITORY <<<
        ├── CMakeLists.txt            # Library build config (target_include_directories)
        ├── README.md
        ├── Makefile
        │
        ├── include/sputteros/        # Namespaced includes for the user
        │   ├── ConfigTraits.h         # Cfg contract documentation & validator
        │   ├── osal/                 # Pure Interfaces (you implement these)
        │   │   ├── SputterTime.h      # 32-bit/64-bit duration types & clock
        │   │   ├── tasks/            # Task abstraction layer
        │   │   │   ├── ITask.h        # Base task with device dependency tracking
        │   │   │   ├── ICriticalTask.h # Deterministic task interface (Core 0)
        │   │   │   └── IAsyncTask.h   # Best-effort task interface (Core 1)
        │   │   │
        │   │   └── sync/             # Synchronization & queuing primitives
        │   │       ├── IMessageQueue.h    # Template: IMessageQueue<Cfg>
        │   │       ├── ICommandProducer.h # Write-only queue (CommsTask)
        │   │       ├── ICommandConsumer.h # Read-only queue (ControlTask)
        │   │       ├── IMutex.h           # Single-core critical section interface
        │   │       ├── LockFreeQueue.h    # Mandatory SPSC queue for cross-core comms
        │   │       ├── ICoreErrorHandler.h # MultiCoreSync error callback interface
        │   │       ├── MultiCoreSync.h     # Multi-core startup/shutdown barrier
        │   │       └── WatchdogSync.h      # Atomic inter-core heartbeat monitor
        │   │
        │   ├── hal/                  # Pure Interfaces (you implement these)
        │   │   ├── base/
        │   │   │   └── ISputterDevice.h
        │   │   ├── devices/
        │   │   │   └── IStreamReader.h
        │   │   └── README.md
        │   │
        │   ├── kernel/               # Microkernel (constructed via KernelConstructTag PassKey)
        │   │   ├── CommsTask.h       # Template: CommsTask<Cfg> (IAsyncTask)
        │   │   ├── ControlTask.h     # Template: ControlTask<Cfg> (ICriticalTask)
        │   │   ├── DiagnosticsTask.h
        │   │   ├── interfaces/
        │   │   │   ├── IUserApplication.h # User-space application interface
        │   │   │   └── ISafetyMonitor.h    # Generic failsafe interface
        │   │   ├── KernelConstructTag.h # PassKey idiom — gates kernel task construction
        │   │   ├── System.h          # System<Cfg> singleton — owns all runtime infrastructure
        │   │   └── TaskTimer.h       # Per-task execution timer
        │   │
        │   ├── builder/              # System construction
        │   │   └── SystemBuilder.h    # Template: declarative wiring → System<Cfg>
        │   │
        │   ├── comms/                # Communication framework
        │   │   └── CLI.h              # Template: stream + parser + builder wrapper
        │   │
        │   ├── interfaces/           # User-space interfaces
        │   │   └── IProcessState.h    # Optional per-phase interface: onEnter/execute/onExit
        │   │
        │   ├── logic/                # Core Logic (supplied, you extend)
        │   │   ├── InterlockManager.h
        │   │   ├── IFaultResponse.h
        │   │   ├── IInterlockCondition.h
        │   │   └── CommandParser.h    # Template: CommandParser<Cfg>
        │   │
        │   ├── utils/                # Standardized Tools (supplied + mocks for testing)
        │   │   ├── MemoryProfiler.h     # Heap/stack high-water mark tracker
        │   │   ├── NonBlockingStopwatch.h # Non-blocking monotonic timer utility
        │   │   ├── PIDController.h       # Discrete PID controller
        │   │   │
        │   │   ├── logging/           # Logging & diagnostic utilities
        │   │   │   ├── ErrorLogger.h        # ISR-safe circular fault log
        │   │   │   ├── TelemetryLogger.h    # Buffered human-readable task telemetry
        │   │   │   └── LightweightStringBuilder.h # Heap-free fixed-capacity formatter
        │   │   │
        │   │   └── mocks/            # Dummy stub for bring-up
        │   │       └── DummyStreamReader.h
        │
        ├── src/                      # SputterOS Core Implementations
        │   ├── kernel/               # ControlTask, CommsTask, DiagnosticsTask
        │   ├── logic/                # InterlockManager
        │   ├── utils/                # Implementation sources
        │   │   ├── MemoryProfiler.cpp
        │   │   ├── NonBlockingStopwatch.cpp
        │   │   ├── PIDController.cpp
        │   │   │
        │   │   └── logging/          # Logging implementations
        │   │       ├── ErrorLogger.cpp
        │   │       ├── TelemetryLogger.cpp
        │   │       └── LightweightStringBuilder.cpp
        │   │
        │   └── ...
        │
        ├── tests/                    # SputterOS host-native test suites
        │   ├── CMakeLists.txt
        │   ├── systemTests/
        │   └── unit/
        │
        └── docs/                     # SputterOS Documentation
            ├── SystemArchitecture.md
            ├── ImplementationGuide.md  ← You are here
            ├── ISRMethodology.md
            ├── MultiCoreImplementation.md
            └── CommentStyle.md
```

## Key Principles

### Responsibility Split

| SputterOS Provides | You Provide |
|---|---|
| `System<Cfg>` singleton - owns all kernel infrastructure | Concrete `IUserApplication<Cfg>` subclass |
| `SystemBuilder<Cfg>` - declarative wiring, creates kernel tasks | Concrete `IProcessState` subclasses (one per process phase) |
| `ControlTask<Cfg>`, `CommsTask<Cfg>`, `DiagnosticsTask` (kernel, private) | `ISafetyMonitor` adapters wrapping your safety logic |
| `LockFreeQueue`, `WatchdogSync`, `MultiCoreSync` (owned by System) | All HAL **implementations** (drivers) |
| `InterlockManager`, `CommandParser<Cfg>`, `CLI<Cfg>` | A `Cfg` struct satisfying the `ConfigTraits` contract |
| `Dummy*` stubs for bring-up | Platform `main.cpp` - SystemBuilder wiring |
| `ConfigTraits.h` **contract** (documents the template) | Real hardware drivers |

### Critical File: `include/config/MyConfig.h` (Your ConfigTraits Struct)

SputterOS uses **compile-time template parameters**. You define a plain struct that satisfies the `ConfigTraits` contract documented in `include/sputteros/ConfigTraits.h`:

```cpp
#pragma once
#include <cstdint>
#include <cstddef>

struct MyConfig {
    enum class State : uint8_t {
        IDLE = 0, ROUGHING, TURBOPUMPING, VENTING, DEPOSITING, FAULT_SAFE_MODE
    };
    enum class CmdID : uint8_t {
        SET_STATE = 0, ABORT_PROCESS, SET_GAS_FLOW, SET_POWER
    };
    struct Command {
        CmdID   id;
        uint8_t targetDevice;
        float   value;
    };

    // Required: core count and queue capacity
    static constexpr std::size_t kCoreCount         = 2;     // 1 or 2
    static constexpr std::size_t kQueueCapacity     = 16;    // > 0

    // Optional (defaults shown)
    static constexpr int         kMaxCommandsPerTick = 8;
    static constexpr uint8_t     kMaxValidCommandID  = 3;    // highest valid CmdID
    // static constexpr uint32_t kControlBudgetUs    = 10000; // cycle budget (µs), default 100 Hz
};
```

### SystemBuilder Wiring in main.cpp

`SystemBuilder<MyConfig>` is the mandatory entry point. It creates kernel tasks, auto-assigns them to cores based on type, and populates the `System<Cfg>` singleton:

```cpp
#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/System.h"
#include "config/MyConfig.h"
#include "MyProcessApp.h"

int main() {
    // 1. Construct HAL drivers and your IUserApplication<Cfg>
    MyVacuumGauge gauge;
    MyRelay contactor;
    MyStreamReader usbStream;
    MyProcessApp app(&gauge, &contactor);

    // 2. Construct ISafetyMonitor adapters (wrap your safety logic)
    InterlockMonitor interlockMon(&interlockMgr);
    std::array<SputterOS::ISafetyMonitor*, 1> monitors = {&interlockMon};

    // 3. Build: SystemBuilder creates ControlTask, CommsTask, DiagnosticsTask internally
    //    and auto-assigns them to cores (ICriticalTask → Core 0, IAsyncTask → Core 1)
    SputterOS::SystemBuilder<MyConfig> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&usbStream);
    builder.setWatchdogKick([]() { /* kick hardware watchdog */ });

    const SputterOS::BuildResult result = builder.build();
    if (!result) {
        // result.error describes the failure
        while (true) {}
    }

    // 4. Run: use the System singleton for init and tick
    SputterOS::System<MyConfig>::init(0);
    while (true) {
        const SputterOS::SputterMicros now = platformGetTimeMicros();
        SputterOS::System<MyConfig>::tick(0, now);
    }
}
```

No CMake include-path injection is needed. See [Step 2 in the Implementation Guide](ImplementationGuide.md#step-2--define-your-config-struct) for details.

### User Implementation Directories

**`include/osal_impl/` + `src/osal_impl/`**
- OSAL (Operating System Abstraction Layer) implementations
- Use `SystemBuilder<Cfg>` - it populates the `System<Cfg>` singleton with a built-in `LockFreeQueue` accessible via `System<Cfg>::commandQueue()`. Direct `LockFreeQueue` instantiation is a compile error (private constructor, friends only `System` and `SystemBuilder`).
- Typical implementations:
  - `PicoHardwareFIFO` wraps the Pico SDK's FIFO for non-command data paths
  - `PicoSpinlock` wraps a spinlock for single-core critical sections
- On FreeRTOS: wrap `xQueueCreate`, `xTaskCreate`

**`include/hal_impl/` + `src/hal_impl/`**
- HAL (Hardware Abstraction Layer) implementations
- Typical implementations per your hardware:
  - USB stream for commands → `PioUsbStreamReader` implements `IStreamReader`
  - Domain-specific devices (pressure sensors, MFCs, PSU, etc.) → define your own interface in your project inheriting `ISputterDevice`
**`include/config/` + `src/`**
- `BoardConfig.h` - GPIO pin assignments, ADC channel mappings
- `ProcessConfig.h` - calibration curves, setpoints (vacuum targets, timings)
- `MyProcessStateMachine.cpp` - your state machine implementation

### CMake Integration

Your `CMakeLists.txt` should include:

```cmake
# Add SputterOS as a subdirectory
add_subdirectory(lib/SputterOS)

# Your project executable
add_executable(my_sputtering_controller
    src/main.cpp
    src/MyProcessApp.cpp
    src/hal_impl/AnalogPiraniGauge.cpp
    src/hal_impl/GPIO_RelayControl.cpp
    # ... add all your implementation files
)

# Link SputterOS library — no include-path injection needed for config
target_link_libraries(my_sputtering_controller PRIVATE SputterOS)

# Include paths for your code
target_include_directories(my_sputtering_controller PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
```

## Minimal Bring-Up Example

To verify the system boots with all dummy stubs:

```cpp
// main.cpp
#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/System.h"
#include "DummyStreamReader.h"  // from tests/unit/mocks/ — copy to your project for bring-up
#include "config/MyConfig.h"
#include "MyProcessApp.h"

int main() {
    // Create dummy hardware
    SputterOS::DummyStreamReader usbStream;

    // Create your IUserApplication<Cfg>
    MyProcessApp app();

    // No safety monitors for bring-up (pass nullptr, 0)
    SputterOS::SystemBuilder<MyConfig> builder(&app, nullptr, 0);
    builder.setStream(&usbStream);

    const SputterOS::BuildResult result = builder.build();
    if (!result) { while (true) {} }

    SputterOS::System<MyConfig>::init(0);

    while (true) {
        const SputterOS::SputterMicros now = platformGetTimeMicros();
        SputterOS::System<MyConfig>::tick(0, now);
    }
}
```

Replace each `Dummy*` stub with your real driver as it's ready - no other code changes needed.

## Next Steps

1. Clone your project and add SputterOS as a submodule:
   ```sh
   git submodule add <repo-url> lib/SputterOS
   ```

2. Create the directory structure above (at minimum the `config/sputteros_config/` folder)

3. Follow the [ImplementationGuide.md](ImplementationGuide.md) step-by-step:
   - Step 1: Add SputterOS to CMake
   - Step 2: Define your `Cfg` struct
   - Steps 3-7: Implement HAL interfaces, `IUserApplication`, process phases, safety monitors, and wire in `main.cpp`

4. Reference [SystemArchitecture.md](SystemArchitecture.md) to understand component responsibilities

5. Use [ISRMethodology.md](ISRMethodology.md) if your drivers use hardware interrupts

6. Reference [MultiCoreImplementation.md](MultiCoreImplementation.md) if distributing across cores
