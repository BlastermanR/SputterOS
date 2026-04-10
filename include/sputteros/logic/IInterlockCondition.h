#ifndef SPUTTEROS_LOGIC_IINTERLOCKCONDITION_H
#define SPUTTEROS_LOGIC_IINTERLOCKCONDITION_H

/**
 * @file IInterlockCondition.h
 * @brief Interface for a single interlock safety condition.
 *
 * Every safety constraint that `InterlockManager` must evaluate implements
 * this interface. `InterlockManager` holds a fixed registry of conditions
 * and calls `isSafe()` on each one every control tick.
 *
 * This keeps the framework machine-agnostic: a sputtering system,
 * nanopositioner, or any other nanofab tool registers its own conditions
 * without modifying the framework.
 *
 * @note Implementations must be non-blocking. `isSafe()` is called on the
 *       hot control-task path and must complete within the tick budget.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/6/2026
 */

namespace SputterOS
{

class IInterlockCondition
{
  public:
    /**
     * @brief Virtual destructor.
     */
    virtual ~IInterlockCondition() = default;

    /**
     * @brief Evaluate whether this condition is currently safe.
     * @return true if the condition is nominal (safe to proceed),
     *         false if the condition is violated.
     */
    virtual bool isSafe() const = 0;

    /**
     * @brief Return a short human-readable name for telemetry logging.
     * @return Null-terminated string identifying this condition
     *         (e.g. "VacuumPressure", "CoolingFlow").
     */
    virtual const char *name() const = 0;

    // Non-copyable — implementations may own hardware resources.
    IInterlockCondition(const IInterlockCondition &)            = delete;
    IInterlockCondition &operator=(const IInterlockCondition &) = delete;

  protected:
    IInterlockCondition() = default;
};

} // namespace SputterOS

#endif // SPUTTEROS_LOGIC_IINTERLOCKCONDITION_H
