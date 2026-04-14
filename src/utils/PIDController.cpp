/**
 * @file PIDController.cpp
 * @brief Implementation of the discrete PID control algorithm.
 *
 * Provides basic proportional-integral-derivative control with output
 * saturation and anti-windup behavior.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/5/26
 */

#include "sputteros/utils/PIDController.h"
#include "sputteros/utils/MinMax.h"

namespace SputterOS
{

PIDController::PIDController(float kp, float ki, float kd, float outputMin, float outputMax)
    : m_kp(kp), m_ki(ki), m_kd(kd), m_outputMin(outputMin), m_outputMax(outputMax), m_integral(0.0f), m_prevError(0.0f),
      m_lastOutput(0.0f), m_lastTimestamp(0), m_firstSample(true)
{
    // Intentionally Empty
}

float PIDController::compute(float setpoint, float measured, SputterMicros timestamp)
{
    // On the very first call after construction or reset, initialise the
    // timestamp only and return 0 — no meaningful dt exists yet.
    if (m_firstSample)
    {
        m_lastTimestamp = timestamp;
        m_firstSample   = false;
        return 0.0f;
    }

    const auto dtUs = timestamp - m_lastTimestamp;
    if (dtUs == 0)
    {
        return m_lastOutput;
    }
    const float dt = static_cast<float>(dtUs) * 0.000001f; // Convert µs → s

    const float error      = setpoint - measured;
    const float derivative = (error - m_prevError) / dt;

    // Accumulate integral with anti-windup: only integrate if the output
    // is not saturated, or if the integral would reduce (not increase) the
    // saturation.
    const float rawIntegral = m_integral + error * dt;
    const float rawOutput   = m_kp * error + m_ki * rawIntegral + m_kd * derivative;
    const bool  saturated   = (rawOutput >= m_outputMax || rawOutput <= m_outputMin);
    const bool  sameSign    = (error * m_integral) > 0.0f;

    if (!saturated || !sameSign)
    {
        m_integral = rawIntegral;
    }

    const float output =
        sput_max(m_outputMin, sput_min(m_outputMax, m_kp * error + m_ki * m_integral + m_kd * derivative));

    m_prevError     = error;
    m_lastOutput    = output;
    m_lastTimestamp = timestamp;
    return output;
}

void PIDController::setGains(float kp, float ki, float kd)
{
    m_kp = kp;
    m_ki = ki;
    m_kd = kd;
}

void PIDController::setOutputLimits(float outputMin, float outputMax)
{
    m_outputMin = outputMin;
    m_outputMax = outputMax;
}

float PIDController::getLastOutput() const { return m_lastOutput; }

void PIDController::reset()
{
    m_integral      = 0.0f;
    m_prevError     = 0.0f;
    m_lastOutput    = 0.0f;
    m_lastTimestamp = 0;
    m_firstSample   = true;
}

} // namespace SputterOS
