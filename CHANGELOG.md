# Changelog

All notable changes to SputterOS will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [v0.5.0] — 2026-04-14

### Added

- **ICrunchTask interface** (`include/sputteros/osal/tasks/ICrunchTask.h`) — exclusive-core,
  blocking-tolerant tight-loop task. Declares `crunch()`, `crunchPeriodUs()`, `maxIterationUs()`,
  and `onCrunchAbort()`. Registered via `CoreBuilder::setCrunchTask()`. (WP-2)
- **CoreDispatchMode enum** (`include/sputteros/kernel/CoreDispatchMode.h`) — per-core dispatch
  strategy: `FLAT_LOOP` (default cooperative scheduler) or `CRUNCH` (exclusive tight loop). (WP-2)
- **CrunchDispatcher** (`include/sputteros/kernel/CrunchDispatcher.h`) — tight-loop dispatcher
  for CRUNCH-mode cores with WCET overrun detection, watchdog kicks, and safety-abort
  forwarding. Fires `onCrunchAbort()` after `kCrunchMaxOverruns` consecutive overruns. (WP-3)
- **AtomicDoubleBuffer** (`include/sputteros/osal/sync/AtomicDoubleBuffer.h`) — wait-free SWSR
  double buffer for latest-value cross-core data sharing. Pluggable `CachePolicy` for
  non-coherent D-cache platforms; default `NoCachePolicy` is zero-cost. (WP-4)
- **Safety abort bridge** — `System<Cfg>::signalSafetyAbort()`, `isSafetyAborted()`,
  `clearSafetyAbort()` for cross-core abort propagation. `ScheduledControlTask` signals
  abort on safety failure; `CrunchDispatcher` checks and forwards to `onCrunchAbort()`. (WP-5)
- **Background task dispatcher** — Phase 2 gap-time background ring on the background core.
  `BackgroundDiagnosticsTask` is always first in the ring and dispatches unconditionally. (WP-1)
- **PlatformAssert.h** (`include/sputteros/utils/PlatformAssert.h`) — `SPUTTEROS_ASSERT` macro
  replacing `<cassert>`. Overridable `SPUTTEROS_FAULT(file, line)` hook for platform-specific
  fault handlers. (WP-7)
- **MinMax.h** (`include/sputteros/utils/MinMax.h`) — `sput_min`/`sput_max` constexpr helpers
  replacing `<algorithm>` usage. (WP-7)
- **crunchloop example** (`exampleProjects/crunchloop/`) — dual-core example with `ICrunchTask`
  on Core 1 (CRUNCH mode) and `IUserApplication` on Core 0 (FLAT_LOOP) sharing data via
  `AtomicDoubleBuffer`. Simulates a multi-joint servo SPI control loop at ~6 kHz. (WP-6)
- Configuration trait `kCrunchMaxOverruns` (default `10`) — consecutive WCET overrun threshold
  before `onCrunchAbort()` fires.
- Configuration trait `kMaxBackgroundTasks` — maximum number of background tasks per core.
- Configuration trait `kMetricsWindowUs` — rolling window duration for scheduler health metrics.

### Removed

- **IAsyncTask.h** — deprecated compatibility shim for `IBackgroundTask`. Use `IBackgroundTask`
  directly. (WP-8)
- **ICriticalTask.h** — deprecated compatibility shim for `IScheduledTask`. Use `IScheduledTask`
  directly. (WP-8)

### Changed

- **System.h** — replaced `<cassert>` / `assert()` with `SPUTTEROS_ASSERT()`. (WP-7)
- **SystemBuilder.h** — removed unused `<cassert>` include. (WP-7)
- **PIDController.cpp** — replaced `<algorithm>` / `std::max`/`std::min` with `MinMax.h` /
  `sput_max`/`sput_min`. (WP-7)
- **CommandParser.h** — replaced `<cctype>` / `std::isspace()` with inline character checks. (WP-7)
- Documentation updated across `SchedulingDesign.md`, `SystemArchitecture.md`,
  `MultiCoreImplementation.md`, `ImplementationGuide.md`, `ExampleProjectStructure.md`,
  `ISRMethodology.md`, and `.github/copilot-instructions.md`. (WP-9)
