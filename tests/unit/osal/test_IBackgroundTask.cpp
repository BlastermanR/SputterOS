/**
 * @file test_IBackgroundTask.cpp
 * @brief Unit tests for IBackgroundTask background scheduling interface.
 *
 * Validates scheduling type markers and default budget value.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "sputteros/osal/tasks/IBackgroundTask.h"
#include <gtest/gtest.h>

using namespace SputterOS;

// ===========================================================================
// Concrete test double
// ===========================================================================

/**
 * @brief Minimal IBackgroundTask implementation for testing defaults.
 */
class ConcreteBackgroundTask : public IBackgroundTask
{
  public:
    void init() override {}
    void tick(SputterMicros /*systemTimeMicros*/) override {}
};

// ===========================================================================
// Scheduling type markers
// ===========================================================================

TEST(IBackgroundTaskTest, IsBackground_ReturnsTrue)
{
    ConcreteBackgroundTask task;
    EXPECT_TRUE(task.isBackground());
}

TEST(IBackgroundTaskTest, IsScheduled_ReturnsFalse)
{
    ConcreteBackgroundTask task;
    EXPECT_FALSE(task.isScheduled());
}

// ===========================================================================
// Default budget
// ===========================================================================

TEST(IBackgroundTaskTest, MaxBudgetUs_DefaultReturns1000)
{
    ConcreteBackgroundTask task;
    EXPECT_EQ(task.maxBudgetUs(), 1000u);
}
