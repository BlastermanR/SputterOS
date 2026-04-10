#ifndef SPUTTEROS_KERNEL_IUSERAPPLICATION_H
#define SPUTTEROS_KERNEL_IUSERAPPLICATION_H

#include "sputteros/osal/SputterTime.h"

/**
 * @file IUserApplication.h
 * @brief Lightweight interface between the kernel and user-space logic.
 *
 * The user implements `IUserApplication` to run their custom control
 * logic, state machines, PID loops, hardware commands, treating all
 * of that as internal implementation details. The kernel's `ControlTask`
 * owns and ticks this interface every control cycle.
 *
 * This decouples the OS kernel from any specific process model (sputtering,
 * etching, etc.). The kernel only knows how to:
 * - Initialize the application
 * - Forward validated commands
 * - Tick the application each cycle
 * - Force a safe abort on safety violations
 *
 * @tparam Cfg Configuration struct providing `Cfg::Command`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */
namespace SputterOS
{

template <typename Cfg> class IUserApplication
{
  public:
    using CommandStruct = typename Cfg::Command;

    /**
     * @brief Virtual destructor.
     */
    virtual ~IUserApplication() = default;

    /**
     * @brief Initialize user application state before the control loop begins.
     *
     * Called once by `ControlTask::init()`. Use this to configure hardware,
     * register states, or seed initial conditions.
     */
    virtual void init() = 0;

    /**
     * @brief Advance the user application by one control cycle.
     * @param systemTimeMicros: Monotonic system time in microseconds
     *        supplied by the platform.
     *
     * Called by `ControlTask::tick()` after safety monitors are verified.
     * Must complete in bounded time.
     */
    virtual void tick(SputterMicros systemTimeMicros) = 0;

    /**
     * @brief Process a validated operator command.
     * @param cmd: Command packet consumed from the shared command queue.
     *
     * Called by `ControlTask` for each command drained from the queue,
     * before `tick()` is called in the same control cycle.
     */
    virtual void handleCommand(const CommandStruct &cmd) = 0;

    /**
     * @brief Force an immediate safe abort of the current process.
     *
     * Called by `ControlTask` when any `ISafetyMonitor` reports a failsafe
     * condition. Must transition to a safe resting state and must not block.
     */
    virtual void forceSafeAbort() = 0;

    // Non-copyable — implementations own domain resources.
    IUserApplication(const IUserApplication &)            = delete;
    IUserApplication &operator=(const IUserApplication &) = delete;

  protected:
    IUserApplication() = default;
};

} // namespace SputterOS

#endif // SPUTTEROS_KERNEL_IUSERAPPLICATION_H
