#ifndef SPUTTEROS_UTILS_PIDCONTROLLER_H
#define SPUTTEROS_UTILS_PIDCONTROLLER_H

#include "sputteros/osal/SputterTime.h"
#include <cstdint>

namespace SputterOS
{

/**
 * @file PIDController.h
 * @brief Platform-agnostic discrete PID controller.
 *
 * Computes a clamped PID-corrected output from a setpoint and measured value.
 * dt is derived from caller-supplied millisecond timestamps so the controller
 * is platform-agnostic and fully unit-testable without hardware timers.
 *
 * @note Anti-windup is applied by clamping the integral term when the output
 *       saturates, preventing runaway during long off-setpoint periods.
 * @note The first `compute()` call after construction or `reset()` initialises
 *       the timestamp only and returns 0; no output is applied on that sample.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */
class PIDController
{
  public:
    /**
     * @brief Construct a PIDController with gains and output clamp limits.
     * @param kp: Proportional gain.
     * @param ki: Integral gain.
     * @param kd: Derivative gain.
     * @param output_min: Minimum allowed output value.
     * @param output_max: Maximum allowed output value.
     */
    PIDController(float kp, float ki, float kd, float output_min, float output_max);

    /**
     * @brief Compute the PID-corrected output for the current sample.
     * @param setpoint: Desired target value.
     * @param measured: Current measured process value.
     * @param timestamp: Current monotonic time used to derive dt since the
     *        previous call.
     * @return Clamped output value in [output_min, output_max].
     *
     * @note Returns 0 and records the timestamp on the first call after reset.
     */
    float compute(float setpoint, float measured, SputterMicros timestamp);

    /**
     * @brief Update PID gains without reconstructing the controller.
     * @param kp: New proportional gain.
     * @param ki: New integral gain.
     * @param kd: New derivative gain.
     */
    void setGains(float kp, float ki, float kd);

    /**
     * @brief Update the output clamping limits.
     * @param output_min: New minimum output value.
     * @param output_max: New maximum output value.
     */
    void setOutputLimits(float output_min, float output_max);

    /**
     * @brief Return the output computed by the most recent `compute()` call.
     * @return Last clamped output, or 0 if no sample has been computed yet.
     */
    float getLastOutput() const;

    /**
     * @brief Reset integral accumulator, derivative history, and timestamp.
     *
     * Call when re-entering a control phase to prevent integral windup
     * carried over from a previous run.
     */
    void reset();

  private:
    float m_kp;        /**< @brief Proportional gain. */
    float m_ki;        /**< @brief Integral gain. */
    float m_kd;        /**< @brief Derivative gain. */
    float m_outputMin; /**< @brief Lower output clamp. */
    float m_outputMax; /**< @brief Upper output clamp. */

    float         m_integral;      /**< @brief Accumulated integral term (anti-windup clamped). */
    float         m_prevError;     /**< @brief Error from the previous sample for derivative. */
    float         m_lastOutput;    /**< @brief Most recently computed clamped output. */
    SputterMicros m_lastTimestamp; /**< @brief Timestamp of the last `compute()` call. */
    bool          m_firstSample;   /**< @brief true until the first `compute()` after reset. */
};

}; // namespace SputterOS
#endif // SPUTTEROS_UTILS_PIDCONTROLLER_H
