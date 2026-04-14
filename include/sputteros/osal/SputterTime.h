#ifndef SPUTTEROS_OSAL_SPUTTERTIME_H
#define SPUTTEROS_OSAL_SPUTTERTIME_H

/**
 * @file SputterTime.h
 * @brief 64-bit microsecond timing foundation for SputterOS.
 *
 * All kernel timestamps and task `tick()` calls use `SputterMicros`, a
 * `uint64_t` alias representing elapsed microseconds. This gives
 * sub-millisecond resolution for control loops and device scheduling
 * while avoiding 32-bit rollover issues (64-bit µs wraps at ~584,942
 * years — effectively never).
 *
 * The platform supplies time through an injectable `MicrosecondSource`
 * function pointer, avoiding any dependency on `std::chrono` for clock
 * access. On the RP2350, this wraps the Pico SDK's native
 * `to_us_since_boot(get_absolute_time())`.
 *
 * `SystemTimer` wraps the injected clock source and exposes
 * convenience getters that return `double` values in standard time
 * units (microseconds, milliseconds, seconds). These are intended
 * for non-hot-path code only (logging, display, configuration).
 *
 * @author Ryan Massie (rmassie)
 * @date 4/9/2026
 */

#include <cstdint>

namespace SputterOS
{

/**
 * @brief 64-bit microsecond timestamp used throughout the kernel.
 *
 * All task `tick()`, state machine, and logging interfaces use this
 * type for monotonic time. Wraps at ~584,942 years — effectively
 * never rolls over in practice.
 */
using SputterMicros = uint64_t;

/**
 * @brief Platform-supplied microsecond clock function pointer.
 *
 * Injected via `SystemBuilder::setClockSource()`. Must return a
 * monotonically increasing `uint64_t` count of microseconds since
 * an arbitrary epoch. Must be safe to call from any core.
 *
 * On the RP2350 (Pico SDK):
 * @code
 *   uint64_t picoGetTimeMicros() {
 *       return to_us_since_boot(get_absolute_time());
 *   }
 * @endcode
 */
using MicrosecondSource = uint64_t (*)();

/**
 * @brief OS-level timer wrapping an injectable microsecond clock source.
 *
 * Provides raw `uint64_t` access for the kernel hot path and
 * `double`-returning convenience getters for non-critical code
 * (logging, display, configuration).
 *
 * Owned by `System<Cfg>` as an `inline static` member and
 * accessible via `System<Cfg>::timer()`.
 *
 * @note The convenience getters use `double` representation.
 *       On targets with no hardware double FPU (e.g. RP2350 Cortex-M33),
 *       these are software-emulated. Use `nowMicros()` in timing-critical
 *       paths.
 */
class SystemTimer
{
  public:
    /**
     * @brief Construct a SystemTimer with an optional clock source.
     * @param src: Platform microsecond clock (nullable for deferred init).
     */
    explicit SystemTimer(MicrosecondSource src = nullptr) : m_source(src), m_epoch(src ? src() : 0) {}

    /**
     * @brief Set or replace the platform clock source.
     *
     * Captures the current clock value as the epoch so that subsequent
     * `nowMicros()` calls return zero-based uptime.
     *
     * @param src: Microsecond clock function pointer.
     */
    void setClockSource(MicrosecondSource src)
    {
        m_source = src;
        m_epoch  = src ? src() : 0;
    }

    /**
     * @brief Query whether a clock source has been injected.
     * @return true if `setClockSource()` has been called with a non-null pointer.
     */
    bool hasClockSource() const { return m_source != nullptr; }

    // -----------------------------------------------------------------
    // Raw Access (hot path — zero overhead)
    // -----------------------------------------------------------------

    /**
     * @brief Read the current time in microseconds from the injected source.
     *
     * Returns zero-based uptime: the raw clock value minus the epoch
     * captured when `setClockSource()` was called.
     *
     * @return Monotonic µs since clock source was set, or 0 if no source.
     */
    SputterMicros nowMicros() const { return m_source ? (m_source() - m_epoch) : 0; }

    // -----------------------------------------------------------------
    // Convenience API (non-hot-path, double representation)
    // -----------------------------------------------------------------

    /**
     * @brief Current time in microseconds as a `double`.
     */
    double microseconds() const { return toMicroseconds(nowMicros()); }

    /**
     * @brief Current time in milliseconds as a `double`.
     */
    double milliseconds() const { return toMilliseconds(nowMicros()); }

    /**
     * @brief Current time in seconds as a `double`.
     */
    double seconds() const { return toSeconds(nowMicros()); }

    // -----------------------------------------------------------------
    // Static Conversion Helpers
    // -----------------------------------------------------------------

    /**
     * @brief Convert a raw µs count to microseconds as `double`.
     * @param us: Raw microsecond value.
     */
    static double toMicroseconds(SputterMicros us) { return static_cast<double>(us); }

    /**
     * @brief Convert a raw µs count to milliseconds as `double`.
     * @param us: Raw microsecond value.
     */
    static double toMilliseconds(SputterMicros us) { return static_cast<double>(us) / 1000.0; }

    /**
     * @brief Convert a raw µs count to seconds as `double`.
     * @param us: Raw microsecond value.
     */
    static double toSeconds(SputterMicros us) { return static_cast<double>(us) / 1000000.0; }

  private:
    MicrosecondSource m_source;   /**< @brief Injected platform clock. */
    SputterMicros     m_epoch{0}; /**< @brief Raw clock value at setClockSource() time. */
};

} // namespace SputterOS

#endif // SPUTTEROS_OSAL_SPUTTERTIME_H
