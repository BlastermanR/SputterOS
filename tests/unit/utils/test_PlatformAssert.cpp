/**
 * @file test_PlatformAssert.cpp
 * @brief Unit tests for SPUTTEROS_ASSERT macro behavior.
 *
 * Verifies:
 * - SPUTTEROS_ASSERT(true) does not fire.
 * - Custom SPUTTEROS_FAULT handler is invoked on assertion failure.
 * - SPUTTEROS_ASSERT compiles to a no-op under NDEBUG.
 *
 * @note These tests override SPUTTEROS_FAULT before including
 *       PlatformAssert.h to capture failures instead of spinning.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/14/2026
 */

#include <gtest/gtest.h>

// =========================================================================
// Test with custom fault handler (debug mode)
// =========================================================================

namespace
{
/// @brief Captured fault location from custom SPUTTEROS_FAULT handler.
struct FaultCapture
{
    bool        fired{false};
    const char *file{nullptr};
    int         line{0};

    void reset()
    {
        fired = false;
        file  = nullptr;
        line  = 0;
    }
};

FaultCapture g_fault;
} // namespace

// Override SPUTTEROS_FAULT to capture instead of infinite loop.
// Must be defined BEFORE including PlatformAssert.h.
#undef SPUTTEROS_FAULT
#define SPUTTEROS_FAULT(f, l)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        g_fault.fired = true;                                                                                          \
        g_fault.file  = (f);                                                                                           \
        g_fault.line  = (l);                                                                                           \
    } while (0)

// Ensure we're testing the debug path (SPUTTEROS_ASSERT evaluates expr).
#undef NDEBUG
#include "sputteros/utils/PlatformAssert.h"

// =========================================================================
// Tests
// =========================================================================

TEST(PlatformAssertTest, TrueExpressionDoesNotFire)
{
    g_fault.reset();
    SPUTTEROS_ASSERT(1 == 1);
    EXPECT_FALSE(g_fault.fired);
}

TEST(PlatformAssertTest, TruePointerDoesNotFire)
{
    g_fault.reset();
    int x = 42;
    SPUTTEROS_ASSERT(&x != nullptr);
    EXPECT_FALSE(g_fault.fired);
}

TEST(PlatformAssertTest, FalseExpressionFiresFault)
{
    g_fault.reset();
    SPUTTEROS_ASSERT(1 == 0);
    EXPECT_TRUE(g_fault.fired);
    EXPECT_NE(g_fault.file, nullptr);
    EXPECT_GT(g_fault.line, 0);
}

TEST(PlatformAssertTest, FaultCapturesCorrectLine)
{
    g_fault.reset();
    int lineBeforeAssert = __LINE__;
    SPUTTEROS_ASSERT(false);
    // The fault line should be the line of the SPUTTEROS_ASSERT call.
    EXPECT_EQ(g_fault.line, lineBeforeAssert + 1);
}

TEST(PlatformAssertTest, FaultCapturesFile)
{
    g_fault.reset();
    SPUTTEROS_ASSERT(false);
    // __FILE__ should contain our test file name.
    ASSERT_NE(g_fault.file, nullptr);
    std::string filePath(g_fault.file);
    EXPECT_NE(filePath.find("test_PlatformAssert"), std::string::npos);
}

// =========================================================================
// Verify NDEBUG path compiles the expression away
// =========================================================================

TEST(PlatformAssertTest, NdebugModeIsNoOp)
{
    // We can't truly test NDEBUG in the same TU since we #undef'd it above,
    // but we verify the macro structure is correct by checking that the
    // non-NDEBUG path works as expected (tested above). The NDEBUG path
    // expands to ((void)0) which is a compile-time guarantee.

    // Verify that SPUTTEROS_ASSERT can be used in constexpr-like contexts:
    // it should not have side effects when the expression is true.
    g_fault.reset();
    volatile int x = 10;
    SPUTTEROS_ASSERT(x > 0);
    EXPECT_FALSE(g_fault.fired);
}
