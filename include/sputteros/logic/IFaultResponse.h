#ifndef SPUTTEROS_LOGIC_IFAULTRESPONSE_H
#define SPUTTEROS_LOGIC_IFAULTRESPONSE_H

/**
 * @file IFaultResponse.h
 * @brief Interface for the hardware shutdown action on a hard fault.
 *
 * `InterlockManager` invokes `execute()` when a hard fault is triggered.
 * The concrete implementation composes whatever hardware steps the
 * specific machine requires (open contactors, close valves, stop motors,
 * etc.) into a single deterministic call.
 *
 * @note `execute()` is called on the hot control-task path and must
 *       complete in bounded time. It must not block.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/6/2026
 */

namespace SputterOS
{

class IFaultResponse
{
  public:
    /**
     * @brief Virtual destructor.
     */
    virtual ~IFaultResponse() = default;

    /**
     * @brief Execute the hardware shutdown sequence for a hard fault.
     *
     * Called exactly once per `triggerHardFault()` invocation. Must
     * de-energize all safety-critical outputs deterministically.
     */
    virtual void execute() = 0;

    // Non-copyable — implementations may own hardware resources.
    IFaultResponse(const IFaultResponse &)            = delete;
    IFaultResponse &operator=(const IFaultResponse &) = delete;

  protected:
    IFaultResponse() = default;
};

} // namespace SputterOS

#endif // SPUTTEROS_LOGIC_IFAULTRESPONSE_H
