# SputterOS ISR Methodology

A guide to interrupt-driven hardware handling within the SputterOS polling-based control architecture.

## Core Principle

**SputterOS is ISR-agnostic.** The control loop (`ControlTask`) runs deterministically at a fixed rate (e.g. 100 Hz) and only polls hardware state via non-blocking HAL methods. No RTOS primitives (semaphores, event flags, condition variables) are in the safety-critical path.

**However,** the concrete HAL implementations *you* provide can use interrupts internally. ISRs are allowed, even recommended for latency-sensitive hardware, as long as they remain an implementation detail hidden behind the HAL interface boundary.

---

## The Polling-Only Control Loop

```
ControlTask<Cfg>::tick(SputterMicros systemTimeMicros) {
    // 1. Evaluate all ISafetyMonitor instances (includes arc, interlocks)
    //    First failure → forceSafeAbort() + return
    evaluateSafety();

    // 2. Poll command queue (non-blocking try_pop from LockFreeQueue)
    while (m_commandQueue->try_pop(cmd)) {
        m_app->handleCommand(cmd);
    }

    // 3. Tick user application
    m_app->tick(systemTimeMicros);
}
```

**Key property:** Every method is non-blocking. The loop has **bounded latency**; it always completes within the configured control budget (default ~10 ms at 100 Hz, set via `kControlBudgetUs` in your config struct).

---

## Where ISRs Live: Inside HAL Implementations

When you implement a HAL interface, you own the driver entirely. You can wire up GPIO interrupts however you want, the rest of SputterOS doesn't know or care.

### Example: Hardware Device with ISR

```cpp
class MyHardwareDevice : public SputterOS::ISputterDevice {
private:
    volatile uint32_t* m_enableReg;  // memory-mapped control register
    std::atomic<bool>  m_faultLatch = false;

    // ISR handler (register with GPIO subsystem separately)
    static void hardwareISR() {
        auto* self = instance();
        self->m_faultLatch.store(true, std::memory_order_release);
        // ISR-level instant shutdown — bypass task layer
        self->executeFastFault();
    }

public:
    // ISR-safe fast fault — disable hardware immediately
    void executeFastFault() override {
        *m_enableReg = 0;
    }

    // The HAL interface: polled by ISafetyMonitor each tick (default 10 ms)
    bool isHealthy() const override {
        return !m_faultLatch.load(std::memory_order_acquire);
    }
};
```

**Key insight:** The ISR latches a boolean *and* calls `executeFastFault()` for sub-microsecond shutdown. The `ISafetyMonitor` adapter reads `isHealthy()` on the next tick. The control loop never blocks, never knows about the ISR, and remains deterministic.

---

## ISR Best Practices for HAL Implementations

### 1. Minimize ISR Critical Section

Do the absolute minimum in the ISR. Delegate state updates to the polling method.

**Poor:**
```cpp
void uartISR() {
    // WRONG: Heavy computation in ISR
    float calibrated = (raw * 0.001f) - 0.5f;
    storeSensorValue(calibrated);
    calculateMovingAverage();
}
```

**Good:**
```cpp
void uartISR() {
    // Latch raw data only
    m_rawSensorValue = readHardwareRegister();
    m_newDataReady = true;
}

// Filtering and averaging happen in the polled method
float readFiltered() const {
    if (m_newDataReady) {
        // Apply filter, calibration, smoothing
        float cal = (m_rawSensorValue * 0.001f) - 0.5f;
        m_filteredValue = m_filter.update(cal);
        m_newDataReady = false;
    }
    return m_filteredValue;
}
```

### 2. Use Atomic Operations, Not Mutexes

ISRs cannot take mutexes — they can't block. Use `std::atomic<>` for all shared state.

```cpp
class MyADCDevice : public SputterOS::ISputterDevice {
private:
    std::atomic<uint16_t> m_adcValue = 0;  // ISR writes, polling reads

public:
    void adcISR() { m_adcValue.store(readADC(), std::memory_order_release); }

    // Polled each tick; apply filtering and conversion here (not in ISR)
    uint16_t readRawSample() const {
        return m_adcValue.load(std::memory_order_acquire);
    }

    bool isHealthy() const override { return true; }
};
```

### 3. Protect Multi-Word Updates with Load-Acquire / Store-Release

If you need to update multiple values atomically, use acquire/release semantics.

```cpp
struct SensorData {
    float value;
    float temperature;
    uint32_t timestamp;
};

class MultiChannelSensor : public SputterOS::ISputterDevice {
private:
    std::atomic<SensorData> m_data = {0, 0, 0};

public:
    void sensorISR() {
        SensorData d{readChannel0(), readTemp(), platformGetTimeMicros()};
        m_data.store(d, std::memory_order_release);
    }

    SensorData read() const {
        return m_data.load(std::memory_order_acquire);
    }

    bool isHealthy() const override { return true; }
};
```

### 4. One-Way Data Flow: ISR -> Polling

The ISR *produces* data (GPIO interrupt → latched state). The polling method *consumes* it. Never the reverse.

**Never do this:**
```cpp
// Wrong: Polling method sets state that ISR reads
void tick(uint32_t ms) override {
    m_config = newConfig;  // polling writes
}

void configISR() {
    if (m_config.updated) { /* react */ }  // ISR reads — RACE CONDITION
}
```

---

## ISR Usage Patterns

### `ISputterDevice::executeFastFault()` — ISR Fast Shutdown

**When to use:** Any device that needs sub-microsecond hardware shutdown in response to a hardware event (overcurrent trip, interlocked fault, etc.).

**Why:** The ISR latches the fault *and* calls `executeFastFault()` immediately. The control loop detects the fault on the next tick via `ISafetyMonitor::checkFailsafe()` → `isHealthy()`.

> **CRITICAL — ISR Context Safety**
> `executeFastFault()` may be called from an interrupt service routine at any time.
> Implementations MUST:
> - Write only to memory-mapped hardware registers or `std::atomic<>` variables
> - NOT block, sleep, or yield
> - NOT acquire any mutex, semaphore, or RTOS lock
> - NOT call `new`, `delete`, or any heap allocator
> - NOT call `printf`, `std::cout`, or any blocking I/O
>
> **Safe:** `*m_enableReg = 0;` (direct register write), `m_flag.store(v, std::memory_order_release);`
> **Unsafe:** `std::mutex::lock()`, `malloc()`, `printf()`, any RTOS call.

```cpp
class MyFaultableDevice : public SputterOS::ISputterDevice {
private:
    volatile uint32_t* m_enableReg;  // memory-mapped GPIO
    std::atomic<bool>  m_faultLatch = false;

    static void hardwareISR() {
        auto* self = instance();
        self->m_faultLatch.store(true, std::memory_order_release);
        self->executeFastFault();  // ISR-level instant shutdown
    }

public:
    void executeFastFault() override {
        *m_enableReg = 0;  // instant hardware disable — ISR-safe
    }

    bool isHealthy() const override {
        return !m_faultLatch.load(std::memory_order_acquire);
    }
};
```

Wrap in an `ISafetyMonitor` adapter so `ControlTask` detects the latched fault each tick:

```cpp
class DeviceFaultMonitor : public SputterOS::ISafetyMonitor {
public:
    explicit DeviceFaultMonitor(MyFaultableDevice* dev) : m_dev(dev) {}
    bool checkFailsafe() override { return m_dev->isHealthy(); }
    const char* name() const override { return "DeviceFaultMonitor"; }
private:
    MyFaultableDevice* m_dev;
};
```

### IStreamReader — ISR Recommended

**Why:** UART/USB RX data arrives asynchronously. Queue received bytes in the ISR; let the polling method drain them.

```cpp
class USBSerialImpl : public IStreamReader {
private:
    static constexpr size_t RX_BUFFER_SIZE = 256;
    std::atomic<size_t> m_rxHead = 0, m_rxTail = 0;
    uint8_t m_rxBuffer[RX_BUFFER_SIZE];

    static void usbRxISR(uint8_t byte) {
        auto self = instance();
        size_t next = (self->m_rxHead + 1) % RX_BUFFER_SIZE;
        if (next != self->m_rxTail) {  // not full
            self->m_rxBuffer[self->m_rxHead] = byte;
            self->m_rxHead.store(next, std::memory_order_release);
        }
    }

public:
    size_t available() override {
        size_t h = m_rxHead.load(std::memory_order_acquire);
        size_t t = m_rxTail.load(std::memory_order_acquire);
        return (h >= t) ? (h - t) : (RX_BUFFER_SIZE - t + h);
    }

    size_t read(uint8_t* buf, size_t len) override {
        size_t h = m_rxHead.load(std::memory_order_acquire);
        size_t t = m_rxTail.load(std::memory_order_acquire);
        size_t available = (h >= t) ? (h - t) : (RX_BUFFER_SIZE - t + h);
        size_t toRead = (len < available) ? len : available;
        for (size_t i = 0; i < toRead; i++) {
            buf[i] = m_rxBuffer[t];
            t = (t + 1) % RX_BUFFER_SIZE;
        }
        m_rxTail.store(t, std::memory_order_release);
        return toRead;
    }

    size_t write(const uint8_t* data, size_t len) override {
        // Non-blocking: write to TX register / queue.
        // May overflow; that's OK — drop data rather than block.
        return uartWrite(data, len);
    }

    bool isConnected() const override { return usbIsEnumerated(); }
};
```

---

## System Integration Guidelines

### 1. ISR Registration Timing

Register ISRs *before* `System::init()` is called, typically in your `main()`:

```cpp
int main() {
    MyFaultableDevice faultDev;
    MyStreamReader    usb;

    // Register ISRs (platform-specific)
    registerGPIOISR(GPIO_FAULT_PIN, &faultDev, &MyFaultableDevice::hardwareISR);
    registerUARTISR(&usb, &MyStreamReader::uartRxISR);

    // Build the system — kernel tasks created internally
    MyProcessApp app(&contactor);
    DeviceFaultMonitor faultMon(&faultDev);
    std::array<SputterOS::ISafetyMonitor*, 1> monitors = {&faultMon};

    SputterOS::SystemBuilder<MyConfig> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&usb);
    builder.setWatchdogKick([]() { /* kick HW watchdog */ });

    const SputterOS::BuildResult result = builder.build();
    if (!result) { while (true) {} }

    // Init and tick via the System singleton
    SputterOS::System<MyConfig>::init(0);
    while (true) {
        const SputterOS::SputterMicros now = platformGetTimeMicros();
        SputterOS::System<MyConfig>::tick(0, now);
    }
}
```

### 2. ISR Signal Isolation

The ISR handler touches *only* the atomic latch. It does not call `ControlTask` methods, does not push to the command queue, does not modify `InterlockManager`.

```cpp
// Good: ISR only latches
void hardwareISR() {
    g_sensorReadyFlag = true;  // atomic store
}

// Bad: ISR reaches into control logic
void badISR() {
    ControlTask::tick();  // NEVER - reentrancy + unbounded latency
    g_commandQueue.push(cmd);  // NEVER - may deadlock on mutex
}
```

### 3. Memory Ordering

Use `std::memory_order_acquire` when reading, `std::memory_order_release` when writing.

```cpp
// ISR writes (produces) with release
m_sensorData.store(value, std::memory_order_release);

// Polling reads (consumes) with acquire
float value = m_sensorData.load(std::memory_order_acquire);
```

This ensures the ISR's write is fully visible to the polling method before the next tick.

### 4. Avoid Blocking Operations in ISRs

- No `printf()` — buffering can block
- No I2C/SPI transactions — they take milliseconds
- No mutex locks — ISRs cannot sleep
- No dynamic allocation — interrupts memory fragmentation

**Rationale:** ISRs interrupt everything. A blocking ISR starves the control loop.

### 5. Keep ISRs Fast

Target sub-microsecond ISR handlers. Complex filtering, PID calculations, and state transitions belong in the polling method.

**Bad:**
```cpp
void sensorISR() {
    float raw = readADC();
    for (int i = 0; i < 100; i++) {
        raw = filter(raw);  // 100 iterations in ISR — too slow
    }
}
```

**Good:**
```cpp
void sensorISR() {
    uint16_t raw = readADC();
    m_sensorRaw.store(raw);  // ~100 ns
}

// Filtering and averaging happen in the polled method:
uint16_t readSample() const {
    uint16_t raw = m_sensorRaw.load();
    // ... filtering happens here in the polling tick
    return filter(raw);
}
```

---

## Performance Comparison

### Polling-Only (Recommended for Most Cases)

| Aspect | Characteristic |
|---|---|
| Latency to detect | One tick period (default ~10 ms) |
| Control loop jitter | Low — no ISRs; deterministic |
| Code complexity | Lower — no atomics, straight I2C/SPI |
| Testing | Host-testable; no real-time OS required |
| Power consumption | Higher — CPU polls continuously |

### With ISRs (Event-Driven Detection)

| Aspect | Characteristic |
|---|---|
| Latency to detect | < 1 us (immediate ISR latch) |
| Control loop jitter | Low — ISR only latches; no blocking calls in ISR |
| Code complexity | Higher — atomics, memory ordering, ISR registration |
| Testing | Requires ISR simulation in tests |
| Power consumption | Lower — CPU sleeps until event |

### Hybrid (Recommended)

- **ISRs for low-latency latch:** Hardware fault/trip events, encoder pulses, UART RX bytes.
- **Polling for data processing:** Sensor averaging, setpoint updates, command routing.

Result: Fast event detection + deterministic control loop + testable code.

---

## Troubleshooting ISR Issues

| Symptom | Likely Cause | Fix |
|---|---|---|
| Intermittent state machine hangs | ISR blocks or ISR-to-polling data race | Ensure ISR only does atomic store; use acquire/release ordering |
| Missed fast-fault events | ISR fires multiple times for one edge | Add debounce in ISR or use edge-triggered interrupt (not level) |
| Garbage sensor values | Multiple ISRs writing same atomic without ordering | Add `std::memory_order_release` to ISR writes |
| Control loop latency increases sporadically | ISR calls blocking function (I2C, printf, malloc) | Move I2C read to polling method; only latch the raw value in ISR |
| Dead lock on command queue | ISR tries to push to queue during control tick | ISRs only write atomics; queue operations are polling-only |
| Unit tests fail on host | ISRs not simulated | Mock ISR behavior with setter methods in test fixtures |

---

## Testing ISRs on Host

Since SputterOS unit tests run on a desktop PC without real interrupts, you must simulate ISR effects. Create a mock device with a setter that simulates the ISR latch:

```cpp
class MockFaultDevice : public SputterOS::ISputterDevice {
private:
    bool m_faultSimulated = false;

public:
    bool isHealthy() const override { return !m_faultSimulated; }
    void executeFastFault() override { /* no-op in tests */ }

    // Test helper: simulate a hardware fault event
    void simulateFault() { m_faultSimulated = true; }
    void clearFault()    { m_faultSimulated = false; }
};

// In test:
MockFaultDevice dev;
DeviceFaultMonitor monitor(&dev);
// Wire monitor into SystemBuilder, then:

dev.simulateFault();
SputterOS::System<TestConfig>::tick(0, SputterMicros(100));
// Next tick evaluateSafety() will call forceSafeAbort()
```

---

## Crunch Tasks and Blocking I/O

`ICrunchTask` (registered via `CoreBuilder::setCrunchTask()`) is the **one sanctioned exception** to the polling-only rule. A crunch task runs on a dedicated core in a bare `while (active) crunch(now)` loop with no Phase 2 background dispatch and no cooperative scheduling.

Because the crunch core is exclusive, bounded blocking I/O is permitted inside `crunch()`:

```cpp
class MyServoTask : public SputterOS::ICrunchTask {
public:
    SputterMicros crunchPeriodUs()  const override { return 500; }  // 2 kHz
    SputterMicros maxIterationUs()  const override { return 400; }  // 400 µs bound

    void crunch(SputterMicros /*now*/) override {
        // Blocking SPI read is OK here — this core is dedicated.
        uint16_t raw = m_spi.blockingRead(SPI_SENSOR_REG, /*timeoutUs=*/200);
        m_pid.update(raw);
        m_spi.blockingWrite(SPI_PWM_REG, m_pid.output());
    }
    // ...
};
```

**Rules for crunch task blocking I/O:**

| Rule | Reason |
|------|--------|
| Block only within `maxIterationUs()` | Overruns beyond this bound count toward `kCrunchMaxOverruns`; persistent overruns call `onCrunchAbort()` |
| Do not share SPI/I2C buses with Core 0 without a mutex | Cross-core bus contention creates non-determinism in `ControlTask` |
| Do not call `ControlTask` methods or push to the command queue from crunch | Same re-entrancy hazard as ISRs |

Blocking I/O is still **forbidden** in `IScheduledTask::tick()` and `IBackgroundTask::tick()`. Only `ICrunchTask::crunch()` on its exclusive core has this permission.

---

## Summary

- **SputterOS control loop is polling-only and ISR-agnostic.**
- **Your HAL implementations *may* use ISRs internally** to latch hardware events into atomics.
- **ISRs stay invisible to the rest of SputterOS.** They only update atomic state; all heavy lifting happens in the polling method.
- **Use ISRs for:** Hardware fault/trip latching, UART/SPI RX byte queuing, high-speed encoder pulses.
- **Use polling for:** Data processing, filtering, calculation, state transitions.
- **Use `ICrunchTask` for:** Exclusive-core, blocking-tolerant tight loops (servo PWM, SPI sensor polling). Bounded blocking only.
- **Memory ordering matters:** `acquire` on read, `release` on write.
- **Keep ISRs fast:** < 1 us; no blocking calls.
- **Simulate ISRs in unit tests** via mock setters since desktop tests have no real interrupts.

This hybrid approach gives you **deterministic safety-critical control** + **low-latency hardware responsiveness** + **host-testable code.**
