/**
 * @file test_ICrunchTask.cpp
 * @brief Unit tests for ICrunchTask crunch-loop interface.
 *
 * Validates scheduling type markers, non-copyability, default
 * `onCrunchAbort()`, and crunch-specific pure virtual enforcement.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/13/2026
 */

#include "sputteros/osal/tasks/ICrunchTask.h"
#include <gtest/gtest.h>

using namespace SputterOS;

// ===========================================================================
// Concrete test double
// ===========================================================================

/**
 * @brief Minimal ICrunchTask implementation for testing defaults.
 */
class ConcreteCrunchTask : public ICrunchTask
{
  public:
    void init() override {}
    void tick(SputterMicros /*systemTimeMicros*/) override {}

    void          crunch(SputterMicros /*now*/) override { ++m_crunchCount; }
    SputterMicros crunchPeriodUs() const override { return 163; }
    SputterMicros maxIterationUs() const override { return 200; }

    uint32_t crunchCount() const { return m_crunchCount; }

  private:
    uint32_t m_crunchCount{0};
};

/**
 * @brief ICrunchTask subclass that overrides onCrunchAbort().
 */
class AbortAwareCrunchTask : public ICrunchTask
{
  public:
    void init() override {}
    void tick(SputterMicros) override {}

    void          crunch(SputterMicros) override {}
    SputterMicros crunchPeriodUs() const override { return 500; }
    SputterMicros maxIterationUs() const override { return 600; }

    void onCrunchAbort() override { m_aborted = true; }

    bool aborted() const { return m_aborted; }

  private:
    bool m_aborted{false};
};

// ===========================================================================
// Scheduling type markers
// ===========================================================================

TEST(ICrunchTaskTest, IsCrunchTask_ReturnsTrue)
{
    ConcreteCrunchTask task;
    EXPECT_TRUE(task.isCrunchTask());
}

TEST(ICrunchTaskTest, IsScheduled_ReturnsFalse)
{
    ConcreteCrunchTask task;
    EXPECT_FALSE(task.isScheduled());
}

TEST(ICrunchTaskTest, IsBackground_ReturnsFalse)
{
    ConcreteCrunchTask task;
    EXPECT_FALSE(task.isBackground());
}

// ===========================================================================
// Base ITask defaults — isCrunchTask is false for non-crunch tasks
// ===========================================================================

TEST(ICrunchTaskTest, BaseITaskIsCrunchTask_ReturnsFalse)
{
    // Verify that a plain ITask subclass returns false for isCrunchTask()
    class PlainTask : public ITask
    {
      public:
        void init() override {}
        void tick(SputterMicros) override {}
    };
    PlainTask plain;
    EXPECT_FALSE(plain.isCrunchTask());
}

// ===========================================================================
// crunch() dispatch
// ===========================================================================

TEST(ICrunchTaskTest, Crunch_IncrementsOnCall)
{
    ConcreteCrunchTask task;
    EXPECT_EQ(task.crunchCount(), 0u);
    task.crunch(1000);
    EXPECT_EQ(task.crunchCount(), 1u);
    task.crunch(2000);
    EXPECT_EQ(task.crunchCount(), 2u);
}

// ===========================================================================
// Declared period and WCET
// ===========================================================================

TEST(ICrunchTaskTest, CrunchPeriodUs_ReturnsDeclaredValue)
{
    ConcreteCrunchTask task;
    EXPECT_EQ(task.crunchPeriodUs(), 163u);
}

TEST(ICrunchTaskTest, MaxIterationUs_ReturnsDeclaredValue)
{
    ConcreteCrunchTask task;
    EXPECT_EQ(task.maxIterationUs(), 200u);
}

// ===========================================================================
// Default onCrunchAbort() — no-op
// ===========================================================================

TEST(ICrunchTaskTest, OnCrunchAbort_DefaultIsNoOp)
{
    ConcreteCrunchTask task;
    // Should not throw or crash
    task.onCrunchAbort();
}

// ===========================================================================
// Custom onCrunchAbort()
// ===========================================================================

TEST(ICrunchTaskTest, OnCrunchAbort_CustomOverrideCalled)
{
    AbortAwareCrunchTask task;
    EXPECT_FALSE(task.aborted());
    task.onCrunchAbort();
    EXPECT_TRUE(task.aborted());
}

// ===========================================================================
// Non-copyable
// ===========================================================================

TEST(ICrunchTaskTest, NonCopyable)
{
    EXPECT_FALSE(std::is_copy_constructible_v<ConcreteCrunchTask>);
    EXPECT_FALSE(std::is_copy_assignable_v<ConcreteCrunchTask>);
}
