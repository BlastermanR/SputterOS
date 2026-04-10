/**
 * @file ICoreErrorHandler.h
 * @brief Application-defined callback interface for MultiCoreSync error events.
 *
 * Implement this interface to receive notification when a core transitions to
 * the ERROR state, either through an explicit `setError()` call or a barrier
 * timeout. The framework calls `onCoreError()` once per event, immediately
 * before the core state is written to ERROR.
 *
 * Typical use: set device-specific error flags, log a diagnostic message,
 * or trigger a safe-shutdown signal visible to the other core.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/6/2026
 */

#ifndef SPUTTEROS_OSAL_ICORE_ERROR_HANDLER_H
#define SPUTTEROS_OSAL_ICORE_ERROR_HANDLER_H

#include <cstddef>

namespace SputterOS
{

/**
 * @brief Application callback invoked when a core enters the ERROR state.
 *
 * Implement in the parent application to integrate MultiCoreSync errors into
 * the application's own error-reporting system (e.g., status registers, printf,
 * telemetry logging).
 */
class ICoreErrorHandler
{
  public:
    virtual ~ICoreErrorHandler() = default;

    /**
     * @brief Called when a core transitions to the ERROR state.
     * @param coreId  Zero-based ID of the core entering ERROR.
     * @param reason  Human-readable description of the error cause.
     *
     * @note This function is called from the context of the faulting core.
     *       Keep it short; do not block.
     * @note May be called from either core concurrently — implement
     *       thread-safely if shared state is touched.
     */
    virtual void onCoreError(std::size_t coreId, const char *reason) = 0;

  protected:
    ICoreErrorHandler()                                     = default;
    ICoreErrorHandler(const ICoreErrorHandler &)            = delete;
    ICoreErrorHandler &operator=(const ICoreErrorHandler &) = delete;
};

} // namespace SputterOS

#endif // SPUTTEROS_OSAL_ICORE_ERROR_HANDLER_H
