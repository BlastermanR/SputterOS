#ifndef SPUTTEROS_HAL_BASE_ISPUTTER_DEVICE_H
#define SPUTTEROS_HAL_BASE_ISPUTTER_DEVICE_H

/**
 * @file ISputterDevice.h
 * @brief Thin base interface for all SputterOS-managed hardware devices.
 *
 * Provides the minimum contract that every hardware device must satisfy
 * so that the `SystemBuilder` can automate initialization, validation,
 * health-checking, and fast-fault hooking without knowing the concrete
 * device type.
 *
 * Concrete device classes inherit from `ISputterDevice` and one or more
 * narrow capability mixins (`IReadablePressure`, `IControllablePower`,
 * `IControllableFlow`, `IProcessControllable`, `ITelemetryProvider`) to
 * declare the features they support.
 *
 * ### Design Rationale
 * - **Thin:** Four methods — avoids forcing unrelated devices to implement
 *   irrelevant operations (e.g. a relay does not need a pressure stub).
 * - **Default-safe:** `init()`, `validate()`, and `executeFastFault()` have
 *   no-op defaults so devices that do not need them compile without stubs.
 * - **`isHealthy()` is pure virtual:** Every device must report its own
 *   health status; there is no meaningful generic default.
 * - **Non-copyable base:** The deleted copy constructor and assignment
 *   operator propagate to all derived interfaces and implementations.
 * - **ISR-safe fast fault:** `executeFastFault()` may be called from ISR
 *   context; implementations must obey the ISR safety contract documented
 *   on that method.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */

namespace SputterOS
{

class ISputterDevice
{
  public:
    /**
     * @brief Virtual destructor.
     */
    virtual ~ISputterDevice() = default;

    /**
     * @brief One-time device initialization.
     *
     * Called by the `SystemBuilder` during the build phase, before the
     * main control loop starts. Implementations should configure hardware
     * registers, communication buses, and default safe states here.
     *
     * @return true if initialization succeeded, false on error.
     */
    virtual bool init() { return true; }

    /**
     * @brief Validate that the device is properly configured and connected.
     *
     * Called after `init()` during the build phase. Implementations should
     * verify bus connectivity, register reads, or self-test results.
     *
     * @return true if the device is valid and ready, false on error.
     */
    virtual bool validate() const { return true; }

    /**
     * @brief Check whether the device is responding and in a safe operating state.
     *
     * Called during startup by `SystemBuilder` (via `HardwareRegistry::allHealthy()`)
     * and periodically in the diagnostics loop. Implementations must inspect
     * the device's last known communication status, sensor range, temperature,
     * or any other condition that warrants corrective action.
     *
     * @return true if the device is operational, false if a fault is present.
     * @note Pure virtual — every concrete device must report its own health.
     */
    virtual bool isHealthy() const = 0;

    /**
     * @brief Immediate hardware fault action — callable from ISR context.
     *
     * Implementations must perform the minimum hardware-safe action to
     * protect equipment (e.g. disable RF output, close gas shutoff)
     * without any blocking calls, mutex acquisition, or memory allocation.
     *
     * @warning **CRITICAL — ISR Context Safety.**
     *          Implementations MUST:
     *          - Write only to memory-mapped registers or `std::atomic<>`.
     *          - NOT block, sleep, or yield.
     *          - NOT acquire any mutex, semaphore, or RTOS lock.
     *          - NOT call `new`, `delete`, or any heap allocator.
     *          - NOT call `printf`, `std::cout`, or any blocking I/O.
     * @note Default implementation is a no-op for devices that have no
     *       immediate hardware cutoff path.
     */
    virtual void executeFastFault() {}

    // Non-copyable — implementations own hardware resources.
    ISputterDevice(const ISputterDevice &)            = delete;
    ISputterDevice &operator=(const ISputterDevice &) = delete;

  protected:
    ISputterDevice() = default;
};

} // namespace SputterOS

#endif // SPUTTEROS_HAL_BASE_ISPUTTER_DEVICE_H
