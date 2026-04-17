# HAL (Hardware Abstraction Layer) Directory Structure

The HAL folder is organized into two tiers:

## Directory Organization

### 1. `base/` — Foundation Interface

| File | Purpose |
|------|---------|
| `ISputterDevice.h` | Thin base interface for all managed hardware devices. Provides `init()`, `validate()`, `isHealthy()`, and `executeFastFault()`. Every device inherits from this. |

---

### 2. `devices/` — Device Interfaces

| Device | Key Methods | Notes |
|--------|-------------|-------|
| `IStream.h` | `available()`, `read()`, `write()`, `isConnected()` | Byte-stream transport (USB/UART). Does not inherit `ISputterDevice`. |

---

## Design Principles

1. **Flat:** Two tiers only — base and devices. No intermediate mixin layer.
2. **Thin Base:** `ISputterDevice` forces no irrelevant methods on subclasses.
3. **User-Defined:** Domain-specific device interfaces (gauges, MFCs, power supplies, etc.) belong in your project, not the kernel library.

---

## Adding a New Device

1. Create a new interface in your project (or in `devices/` if it belongs in the kernel).
2. Inherit `ISputterDevice` and declare only the methods that hardware actually provides.
3. Implement the concrete driver in your project and inject it into `IUserApplication` or wrap it in an `ISafetyMonitor` adapter.
