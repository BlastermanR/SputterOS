/**
 * @file test_IScheduledTask.cpp
 * @brief Unit tests for IScheduledTask scheduling interface.
 *
 * Validates default return values for the scheduling API and
 * verifies that concrete subclasses must implement periodUs().
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "sputteros/osal/tasks/IScheduledTask.h"
#include <gtest/gtest.h>

using namespace SputterOS;

// ===========================================================================
// Concrete test double
// ===========================================================================

/**
 * @brief Minimal IScheduledTask implementation for testing defaults.
 */
class ConcreteScheduledTask : public IScheduledTask
{
  public:
    void          init() override {}
    void          tick(SputterMicros /*systemTimeMicros*/) override {}
    SputterMicros periodUs() const override { return 10000; } // 10 ms
};

// ===========================================================================
// Scheduling type markers
// ===========================================================================

TEST(IScheduledTaskTest, IsScheduled_ReturnsTrue)
{
    ConcreteScheduledTask task;
    EXPECT_TRUE(task.isScheduled());
}

TEST(IScheduledTaskTest, IsBackground_ReturnsFalse)
{
    ConcreteScheduledTask task;
    EXPECT_FALSE(task.isBackground());
}

// ===========================================================================
// Default virtual method values
// ===========================================================================

TEST(IScheduledTaskTest, DeclaredWcetUs_DefaultReturnsZero)
{
    ConcreteScheduledTask task;
    EXPECT_EQ(task.declaredWcetUs(), 0u);
}

TEST(IScheduledTaskTest, SchedulePriority_DefaultReturns0xFF)
{
    ConcreteScheduledTask task;
    EXPECT_EQ(task.schedulePriority(), 0xFF);
}

TEST(IScheduledTaskTest, IsIoPending_DefaultReturnsFalse)
{
    ConcreteScheduledTask task;
    EXPECT_FALSE(task.isIoPending());
}

// ===========================================================================
// periodUs — concrete subclass
// ===========================================================================

TEST(IScheduledTaskTest, PeriodUs_ReturnsDeclaredValue)
{
    ConcreteScheduledTask task;
    EXPECT_EQ(task.periodUs(), 10000u);
}
