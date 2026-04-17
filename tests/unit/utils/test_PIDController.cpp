/**
 * @file test_PIDController.cpp
 * @brief Unit tests for PIDController.
 *
 * Injects controlled timestamps and setpoints to verify gain paths,
 * integral accumulation, derivative computation, output clamping, and
 * anti-windup behaviour without any platform hardware.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */

#include "sputteros/utils/PIDController.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace SputterOS;

/// Helper: convert millisecond-level values to SputterMicros for readability.
static constexpr SputterMicros ms(uint64_t milliseconds) { return milliseconds * 1000u; }

// ===========================================================================
// Fixture
// ===========================================================================

class PIDControllerTest : public ::testing::Test
{
  protected:
    // Tolerance for float comparisons
    static constexpr float kEps = 1e-4f;
};

// ---------------------------------------------------------------------------
// First call always returns 0 (timestamp-only seed)
// ---------------------------------------------------------------------------

TEST_F(PIDControllerTest, FirstCall_ReturnsZero)
{
    PIDController pid(1.0f, 0.0f, 0.0f, -100.0f, 100.0f);
    EXPECT_NEAR(pid.compute(10.0f, 0.0f, ms(0)), 0.0f, kEps);
}

// ---------------------------------------------------------------------------
// Proportional only: output == Kp * error
// ---------------------------------------------------------------------------

TEST_F(PIDControllerTest, ProportionalOnly_OutputEqualsKpTimesError)
{
    PIDController pid(2.0f, 0.0f, 0.0f, -1000.0f, 1000.0f);
    pid.compute(10.0f, 0.0f, ms(0)); // seed
    // error = 10.0 - 5.0 = 5.0  → output = 2.0 * 5.0 = 10.0
    float out = pid.compute(10.0f, 5.0f, ms(1000));
    EXPECT_NEAR(out, 10.0f, kEps);
}

TEST_F(PIDControllerTest, ProportionalOnly_NegativeError_NegativeOutput)
{
    PIDController pid(3.0f, 0.0f, 0.0f, -1000.0f, 1000.0f);
    pid.compute(0.0f, 0.0f, ms(0));
    // error = 0.0 - 5.0 = -5.0 → output = -15.0
    float out = pid.compute(0.0f, 5.0f, ms(1000));
    EXPECT_NEAR(out, -15.0f, kEps);
}

// ---------------------------------------------------------------------------
// Integral: accumulates error * dt each call
// ---------------------------------------------------------------------------

TEST_F(PIDControllerTest, IntegralTerm_AccumulatesOverTwoSamples)
{
    // Ki = 1, Kp = 0, Kd = 0, dt = 1 s each, setpoint = 10, measured = 0
    // After sample 1 (t=1000): integral = 10*1 = 10  → output = 10
    // After sample 2 (t=2000): integral = 10+10*1=20 → output = 20
    PIDController pid(0.0f, 1.0f, 0.0f, -1000.0f, 1000.0f);
    pid.compute(10.0f, 0.0f, ms(0)); // seed
    float out1 = pid.compute(10.0f, 0.0f, ms(1000));
    EXPECT_NEAR(out1, 10.0f, kEps);
    float out2 = pid.compute(10.0f, 0.0f, ms(2000));
    EXPECT_NEAR(out2, 20.0f, kEps);
}

// ---------------------------------------------------------------------------
// Derivative: zero on second call (same error as first)
// ---------------------------------------------------------------------------

TEST_F(PIDControllerTest, DerivativeTerm_ZeroWhenErrorUnchanged)
{
    // prevError is NOT stored on the seed call (it stays 0.0f from construction).
    // Three calls are needed: seed → first real sample (sets prevError) → test call.
    PIDController pid(0.0f, 0.0f, 1.0f, -1000.0f, 1000.0f);
    pid.compute(5.0f, 0.0f, ms(0));                // seed — prevError stays 0
    pid.compute(5.0f, 0.0f, ms(1000));             // sets prevError = 5
    float out = pid.compute(5.0f, 0.0f, ms(2000)); // same error → d = 0
    EXPECT_NEAR(out, 0.0f, kEps);
}

TEST_F(PIDControllerTest, DerivativeTerm_CorrectOnSecondSample)
{
    // prevError is NOT set on the seed call.  Three calls needed:
    // seed → establish prevError=10 → measure derivative against it.
    // dt = 1 s each; error goes 10 → 5 on the third call.
    PIDController pid(0.0f, 0.0f, 2.0f, -1000.0f, 1000.0f);
    pid.compute(10.0f, 0.0f, ms(0));    // seed — prevError stays 0
    pid.compute(10.0f, 0.0f, ms(1000)); // sets prevError = 10
    float out = pid.compute(5.0f, 0.0f, ms(2000));
    // d = (5 - 10) / 1 = -5 → Kd * d = 2.0 * -5 = -10
    EXPECT_NEAR(out, -10.0f, kEps);
}

// ---------------------------------------------------------------------------
// Output clamping to [outputMin, outputMax]
// ---------------------------------------------------------------------------

TEST_F(PIDControllerTest, OutputClampedToMax)
{
    PIDController pid(100.0f, 0.0f, 0.0f, 0.0f, 50.0f);
    pid.compute(10.0f, 0.0f, ms(0));                // seed
    float out = pid.compute(10.0f, 0.0f, ms(1000)); // raw = 1000, clamped to 50
    EXPECT_NEAR(out, 50.0f, kEps);
}

TEST_F(PIDControllerTest, OutputClampedToMin)
{
    PIDController pid(100.0f, 0.0f, 0.0f, -50.0f, 0.0f);
    pid.compute(0.0f, 0.0f, ms(0));
    float out = pid.compute(0.0f, 10.0f, ms(1000)); // error = -10 → raw = -1000
    EXPECT_NEAR(out, -50.0f, kEps);
}

// ---------------------------------------------------------------------------
// Anti-windup: integral does not grow when saturated in the same direction
// ---------------------------------------------------------------------------

TEST_F(PIDControllerTest, AntiWindup_IntegralDoesNotGrowWhenSaturated)
{
    PIDController pid(0.0f, 1.0f, 0.0f, 0.0f, 10.0f);
    pid.compute(100.0f, 0.0f, ms(0)); // seed

    // Drive into positive saturation for many samples; integral should not
    // continue to grow indefinitely (anti-windup engaged).
    float prevOut = 0.0f;
    for (int i = 1; i <= 20; ++i)
    {
        float out = pid.compute(100.0f, 0.0f, ms(static_cast<int64_t>(i) * 1000));
        // Each call must still return the clamped max
        EXPECT_NEAR(out, 10.0f, kEps);
        (void)prevOut;
        prevOut = out;
    }
}

// ---------------------------------------------------------------------------
// reset() restores first-sample behaviour
// ---------------------------------------------------------------------------

TEST_F(PIDControllerTest, Reset_ClearsIntegralAndPrevError)
{
    PIDController pid(0.0f, 1.0f, 0.0f, -1000.0f, 1000.0f);
    pid.compute(10.0f, 0.0f, ms(0));
    pid.compute(10.0f, 0.0f, ms(1000)); // integral = 10

    pid.reset();

    // After reset, first call is a seed
    float out = pid.compute(10.0f, 0.0f, ms(2000));
    EXPECT_NEAR(out, 0.0f, kEps);
}

TEST_F(PIDControllerTest, Reset_AfterReset_IntegralStartsFresh)
{
    PIDController pid(0.0f, 1.0f, 0.0f, -1000.0f, 1000.0f);
    // Build up integral
    pid.compute(10.0f, 0.0f, ms(0));
    pid.compute(10.0f, 0.0f, ms(1000)); // integral = 10

    pid.reset();

    pid.compute(5.0f, 0.0f, ms(0));                // seed again
    float out = pid.compute(5.0f, 0.0f, ms(1000)); // integral = 5*1 = 5
    EXPECT_NEAR(out, 5.0f, kEps);
}

// ---------------------------------------------------------------------------
// Repeated timestamp: output returns cached last value (no divide-by-zero)
// ---------------------------------------------------------------------------

TEST_F(PIDControllerTest, SameTimestamp_ReturnsCachedLastOutput)
{
    PIDController pid(1.0f, 0.0f, 0.0f, -1000.0f, 1000.0f);
    pid.compute(10.0f, 0.0f, ms(0)); // seed
    float out1 = pid.compute(10.0f, 0.0f, ms(1000));
    float out2 = pid.compute(10.0f, 0.0f, ms(1000)); // same timestamp
    EXPECT_NEAR(out1, out2, kEps);
}

// ---------------------------------------------------------------------------
// getLastOutput
// ---------------------------------------------------------------------------

TEST_F(PIDControllerTest, GetLastOutput_ReturnsLastComputed)
{
    PIDController pid(2.0f, 0.0f, 0.0f, -1000.0f, 1000.0f);
    pid.compute(10.0f, 5.0f, ms(0));
    float out = pid.compute(10.0f, 5.0f, ms(1000)); // error = 5 → 10
    EXPECT_NEAR(pid.getLastOutput(), out, kEps);
}

// ---------------------------------------------------------------------------
// setGains / setOutputLimits take effect on the next compute
// ---------------------------------------------------------------------------

TEST_F(PIDControllerTest, SetGains_AffectsNextCompute)
{
    PIDController pid(1.0f, 0.0f, 0.0f, -1000.0f, 1000.0f);
    pid.compute(10.0f, 0.0f, ms(0)); // seed with Kp=1
    pid.setGains(2.0f, 0.0f, 0.0f);
    float out = pid.compute(10.0f, 0.0f, ms(1000)); // Kp=2 → 20
    EXPECT_NEAR(out, 20.0f, kEps);
}
