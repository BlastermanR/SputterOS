#ifndef SPUTTEROS_LOGIC_INTERLOCKMANAGER_H
#define SPUTTEROS_LOGIC_INTERLOCKMANAGER_H

#include "sputteros/OpResult.h"
#include "sputteros/logic/IFaultResponse.h"
#include "sputteros/logic/IInterlockCondition.h"
#include <atomic>
#include <cstddef>

/**
 * @file InterlockManager.h
 * @brief Machine-agnostic safety interlock authority.
 *
 * Acts as the absolute gatekeeper before any hardware change is enacted.
 * Domain-specific safety conditions are registered at startup via
 * `registerCondition()`, and a single `IFaultResponse` is set via
 * `setFaultResponse()` to define the hardware shutdown action on a
 * hard fault.
 *
 * - **Hard interlock:** latched fault requiring an explicit operator clear.
 *   Calls `IFaultResponse::execute()` and cannot be cleared by software
 *   alone while conditions remain violated.
 * - **Soft interlock:** software-forced abort that transitions the state
 *   machine to `IDLE` or other failsafe without latching.
 *
 * @note The `InterlockManager` must be evaluated by `ControlTask` on every
 *       control tick before the `StateMachine` is allowed to act.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/6/2026
 */
namespace SputterOS
{

class InterlockManager
{
  public:
    /**
     * @brief Maximum number of interlock conditions that can be registered.
     */
    static constexpr std::size_t kMaxConditions = 8;

    /**
     * @brief Construct an InterlockManager with no registered conditions.
     *
     * Call `registerCondition()` and `setFaultResponse()` before the
     * control loop starts. `validateDependencies()` will return false
     * until at least one condition and a fault response are registered.
     */
    InterlockManager();

    /**
     * @brief Register an interlock condition to be evaluated each tick.
     * @param condition: Pointer to a condition whose lifetime must exceed
     *        the InterlockManager. Must not be null.
     * @return `OpResult::OK` on success, `OpResult::NULL_ARG` if null,
     *         `OpResult::FULL` if the registry is at capacity.
     */
    OpResult registerCondition(IInterlockCondition *condition);

    /**
     * @brief Set the fault response executed on a hard fault.
     * @param response: Pointer to the shutdown action whose lifetime must
     *        exceed the InterlockManager. Must not be null.
     */
    void setFaultResponse(IFaultResponse *response);

    /**
     * @brief Evaluate all registered interlock conditions.
     * @return true if all conditions are nominal (safe to proceed),
     *         false if any condition is violated.
     *
     * @note A false return means the state machine must abort immediately.
     *       This does NOT automatically trip a hard fault; call
     *       `triggerHardFault()` explicitly when a latching response is needed.
     */
    bool checkAllInterlocks() const;

    /**
     * @brief Trigger a soft abort — sets a flag for the state machine to
     *        transition to `IDLE` or other failsafe on its next evaluation.
     */
    void triggerSoftAbort();

    /**
     * @brief Trigger a hard fault — calls `IFaultResponse::execute()` and
     *        latches a fault flag that requires an explicit operator clear.
     */
    void triggerHardFault();

    /**
     * @brief Query whether a soft abort has been requested.
     * @return true if `triggerSoftAbort()` has been called and not yet cleared.
     */
    bool hasSoftAbort() const;

    /**
     * @brief Query whether the system is in a latched hard fault state.
     * @return true if a hard fault is active.
     */
    bool isHardFaulted() const;

    /**
     * @brief Attempt to clear a latched fault.
     *
     * Only succeeds if all registered interlock conditions are currently
     * nominal. Clears both the soft abort and hard fault flags.
     * @return true if the fault was successfully cleared, false if conditions
     *         still require the fault to remain active.
     */
    bool clearFault();

    /**
     * @brief Validate that at least one condition and a fault response are set.
     * @return true if the manager is properly configured, false otherwise.
     *
     * Call this in `main()` before starting the control loop. If this returns
     * false the system should halt safely before hardware runs.
     */
    bool validateDependencies() const;

  private:
    IInterlockCondition *m_conditions[kMaxConditions]; /**< @brief Registered condition pointers. */
    std::size_t          m_conditionCount;             /**< @brief Number of registered conditions. */
    IFaultResponse      *m_faultResponse;              /**< @brief Shutdown action for hard faults. */
    std::atomic<bool>    m_softAbort; /**< @brief Soft abort pending flag (atomic for cross-context reads). */
    std::atomic<bool>    m_hardFault; /**< @brief Latched hard fault flag (atomic for cross-context reads). */
};

} // namespace SputterOS

#endif // SPUTTEROS_LOGIC_INTERLOCKMANAGER_H
