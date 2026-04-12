#ifndef SPUTTEROS_OSAL_ITASK_H
#define SPUTTEROS_OSAL_ITASK_H

#include "sputteros/OpResult.h"
#include "sputteros/kernel/TaskTimer.h"
#include "sputteros/osal/SputterTime.h"
#include <cstddef>
#include <cstdint>

namespace SputterOS
{

class ISputterDevice; // forward declaration for device dependency tracking

/**
 * @file ITask.h
 * @brief Interface for an OS abstraction task.
 *
 * Defines the task lifecycle interface used by the SputterOS scheduler.
 * Each task implements `init()` for one-time setup and `tick()` for
 * periodic work. The `run()` method provides a default execution loop
 * that can be overridden for RTOS integration.
 *
 * Tasks may register hardware device dependencies via `addDevice()`.
 * Override `validateDependencies()` to declare required devices;
 * `SystemBuilder::build()` calls this on every task before the system
 * starts and fails the build if any task returns false.
 *
 * Subclasses `IScheduledTask` and `IBackgroundTask` provide the
 * scheduling contract used by the Cruncher dispatcher. Override
 * `onSuspend()` / `onResume()` for tasks that need to react to
 * scheduler lifecycle transitions.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */

/**
 * @brief Platform time-source function pointer type.
 *
 * Injected into `run()` so the task loop can obtain the current
 * monotonic time without a platform-specific dependency.
 * Returns microseconds as a raw `uint64_t` (`SputterMicros`).
 */
using TimeSourceFn = SputterMicros (*)();

class ITask
{
  public:
    /**
     * @brief Virtual destructor.
     */
    virtual ~ITask() = default;

    /**
     * @brief Initialize task state before runtime.
     */
    virtual void init() = 0;

    /**
     * @brief Run periodic task work.
     * @param systemTimeMicros: Monotonic system time in microseconds
     *        supplied by the platform scheduler. Passed down to any
     *        internal system that requires delta-time calculations
     *        (PIDs, timeouts, etc.).
     *
     * To be called in a loop by the scheduler.
     */
    virtual void tick(SputterMicros systemTimeMicros) = 0;

    /**
     * @brief Execute the task in a blocking loop (bare-metal default).
     *
     * Calls `init()` once, then enters a `while (true)` loop calling
     * `tick()` with the system time obtained from `getTime`. The loop
     * runs forever on bare-metal targets.
     *
     * Override this method to integrate with an RTOS scheduler (e.g.
     * wrapping `tick()` in a `vTaskDelayUntil()` loop for FreeRTOS).
     *
     * @param getTime Platform-supplied function that returns the current
     *        monotonic time as `SputterMicros`. Must not be nullptr.
     */
    virtual void run(TimeSourceFn getTime)
    {
        init();
        while (true)
        {
            tick(getTime());
        }
    }

    /**
     * @brief Scheduling type marker — true if this task is deadline-scheduled.
     * @return false by default; overridden to true by `IScheduledTask`.
     */
    virtual bool isScheduled() const { return false; }

    /**
     * @brief Scheduling type marker — true if this task runs as background work.
     * @return false by default; overridden to true by `IBackgroundTask`.
     */
    virtual bool isBackground() const { return false; }

    /**
     * @brief Called by the scheduler when this task is being suspended.
     *
     * Override to save state or release shared resources before the
     * scheduler pauses this task. The default implementation is a no-op.
     */
    virtual void onSuspend() {}

    /**
     * @brief Called by the scheduler when this task is being resumed.
     *
     * Override to restore state or reacquire resources after the
     * scheduler resumes this task. The default implementation is a no-op.
     */
    virtual void onResume() {}

    // -----------------------------------------------------------------
    // Device Dependency Registration
    // -----------------------------------------------------------------

    /** @brief Maximum hardware devices registerable per task. */
    static constexpr std::size_t kMaxDevices = 16;

    /**
     * @brief Register a hardware device dependency.
     *
     * Call this in your task's setter methods to register each device
     * the task requires. `SystemBuilder::build()` will later call
     * `validateDependencies()` to verify all required devices are present.
     *
     * @param device Non-null pointer to a device whose lifetime must
     *        exceed the system runtime.
     * @return `OpResult::OK` on success, `OpResult::NULL_ARG` if null,
     *         `OpResult::FULL` if the device array is at capacity.
     */
    OpResult addDevice(ISputterDevice *device)
    {
        if (!device)
        {
            return OpResult::NULL_ARG;
        }
        if (m_deviceCount >= kMaxDevices)
        {
            return OpResult::FULL;
        }
        m_devices[m_deviceCount++] = device;
        return OpResult::OK;
    }

    /**
     * @brief Get a registered device pointer by index.
     * @param idx Zero-based index into the device array.
     * @return Device pointer, or nullptr if out of range.
     */
    ISputterDevice *device(std::size_t idx) const { return (idx < m_deviceCount) ? m_devices[idx] : nullptr; }

    /**
     * @brief Number of devices currently registered on this task.
     */
    std::size_t deviceCount() const { return m_deviceCount; }

    /**
     * @brief Validate that all required dependencies are satisfied.
     *
     * Override this method to check that your task's required devices
     * and other dependencies have been provided. Called by
     * `SystemBuilder::build()` on every registered task; returning
     * false fails the build.
     *
     * The default implementation returns true (no dependencies).
     *
     * @return true if all dependencies are met, false otherwise.
     */
    virtual bool validateDependencies() const { return true; }

    // -----------------------------------------------------------------
    // Task Timer
    // -----------------------------------------------------------------

    /**
     * @brief Access this task's embedded performance timer.
     *
     * Instrumented automatically by `SystemBuilder::tickCore()`.
     * `DiagnosticsTask` reads all task timers to detect overruns.
     *
     * @return Mutable reference to the per-task timer.
     */
    Kernel::TaskTimer &timer() { return m_timer; }

    /**
     * @brief Const access to this task's embedded performance timer.
     */
    const Kernel::TaskTimer &timer() const { return m_timer; }

  private:
    Kernel::TaskTimer m_timer; /**< @brief Per-task execution timing. */

    ISputterDevice *m_devices[kMaxDevices] = {}; /**< @brief Registered device pointers. */
    std::size_t     m_deviceCount          = 0;  /**< @brief Number of registered devices. */
};

} // namespace SputterOS

#endif // SPUTTEROS_OSAL_ITASK_H
