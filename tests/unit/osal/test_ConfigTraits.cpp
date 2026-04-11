/**
 * @file test_ConfigTraits.cpp
 * @brief Unit tests for ConfigTraits SFINAE extractors and type traits.
 *
 * Validates default fallback values for all scheduling config extractors,
 * override behaviour when a user config provides the field, and the
 * IsScheduledTask/IsBackgroundTask type traits.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "sputteros/ConfigTraits.h"
#include "sputteros/osal/tasks/IBackgroundTask.h"
#include "sputteros/osal/tasks/IScheduledTask.h"
#include <gtest/gtest.h>

using namespace SputterOS;

// ===========================================================================
// Minimal config structs for SFINAE tests
// ===========================================================================

namespace
{

/// Config with no optional fields — all extractors should return defaults.
struct MinimalCfg
{
    enum class State { IDLE };
    enum class CmdID : uint8_t { NOP };
    struct Command
    {
        CmdID   id;
        uint8_t targetDevice;
        float   value;
    };
    static constexpr std::size_t kCoreCount     = 2;
    static constexpr std::size_t kQueueCapacity = 8;
};

/// Config that overrides every scheduling field.
struct FullSchedulingCfg
{
    enum class State { IDLE };
    enum class CmdID : uint8_t { NOP };
    struct Command
    {
        CmdID   id;
        uint8_t targetDevice;
        float   value;
    };
    static constexpr std::size_t kCoreCount     = 2;
    static constexpr std::size_t kQueueCapacity = 8;

    static constexpr std::size_t  kMaxSlotsPerCore      = 64;
    static constexpr std::size_t  kMaxBackgroundTasks    = 8;
    static constexpr SputterMicros kCommsBudgetUs        = 500;
    static constexpr SputterMicros kDiagsBudgetUs        = 5000;
    static constexpr SputterMicros kMinGapSliceUs        = 50;
    static constexpr SputterMicros kMinSchedulePeriodUs  = 100;
    static constexpr bool          kStrictWCET           = true;
    static constexpr SputterMicros kIsrContextBudgetUs[2] = {200, 300};
};

/// Stub scheduled task for type trait tests.
class StubScheduled : public IScheduledTask
{
  public:
    SputterMicros periodUs() const override { return 10000; }
    void init() override {}
    void tick(SputterMicros) override {}
};

/// Stub background task for type trait tests.
class StubBackground : public IBackgroundTask
{
  public:
    void init() override {}
    void tick(SputterMicros) override {}
};

/// Plain class that is neither scheduled nor background.
class PlainClass
{
};

} // namespace

// ===========================================================================
// Default value tests
// ===========================================================================

TEST(CfgMaxSlotsPerCore, Default_Returns32)
{
    EXPECT_EQ(CfgMaxSlotsPerCore<MinimalCfg>::value, 32u);
}

TEST(CfgMaxSlotsPerCore, Override_ReturnsCustomValue)
{
    EXPECT_EQ(CfgMaxSlotsPerCore<FullSchedulingCfg>::value, 64u);
}

TEST(CfgMaxBackgroundTasks, Default_Returns16)
{
    EXPECT_EQ(CfgMaxBackgroundTasks<MinimalCfg>::value, 16u);
}

TEST(CfgMaxBackgroundTasks, Override_ReturnsCustomValue)
{
    EXPECT_EQ(CfgMaxBackgroundTasks<FullSchedulingCfg>::value, 8u);
}

TEST(CfgCommsBudgetUs, Default_Returns1000)
{
    EXPECT_EQ(CfgCommsBudgetUs<MinimalCfg>::value, 1000u);
}

TEST(CfgCommsBudgetUs, Override_ReturnsCustomValue)
{
    EXPECT_EQ(CfgCommsBudgetUs<FullSchedulingCfg>::value, 500u);
}

TEST(CfgDiagsBudgetUs, Default_Returns10000)
{
    EXPECT_EQ(CfgDiagsBudgetUs<MinimalCfg>::value, 10000u);
}

TEST(CfgDiagsBudgetUs, Override_ReturnsCustomValue)
{
    EXPECT_EQ(CfgDiagsBudgetUs<FullSchedulingCfg>::value, 5000u);
}

TEST(CfgMinGapSliceUs, Default_Returns10)
{
    EXPECT_EQ(CfgMinGapSliceUs<MinimalCfg>::value, 10u);
}

TEST(CfgMinGapSliceUs, Override_ReturnsCustomValue)
{
    EXPECT_EQ(CfgMinGapSliceUs<FullSchedulingCfg>::value, 50u);
}

TEST(CfgMinSchedulePeriodUs, Default_Returns10)
{
    EXPECT_EQ(CfgMinSchedulePeriodUs<MinimalCfg>::value, 10u);
}

TEST(CfgMinSchedulePeriodUs, Override_ReturnsCustomValue)
{
    EXPECT_EQ(CfgMinSchedulePeriodUs<FullSchedulingCfg>::value, 100u);
}

TEST(CfgStrictWCET, Default_ReturnsFalse)
{
    EXPECT_FALSE(CfgStrictWCET<MinimalCfg>::value);
}

TEST(CfgStrictWCET, Override_ReturnsTrue)
{
    EXPECT_TRUE(CfgStrictWCET<FullSchedulingCfg>::value);
}

TEST(CfgIsrContextBudgetUs, Default_ReturnsZeros)
{
    EXPECT_EQ(CfgIsrContextBudgetUs<MinimalCfg>::value[0], 0u);
    EXPECT_EQ(CfgIsrContextBudgetUs<MinimalCfg>::value[1], 0u);
}

TEST(CfgIsrContextBudgetUs, Override_ReturnsPerCoreValues)
{
    EXPECT_EQ(CfgIsrContextBudgetUs<FullSchedulingCfg>::value[0], 200u);
    EXPECT_EQ(CfgIsrContextBudgetUs<FullSchedulingCfg>::value[1], 300u);
}

// ===========================================================================
// Type trait tests
// ===========================================================================

TEST(IsScheduledTask, Positive_DetectsSubclass)
{
    EXPECT_TRUE(IsScheduledTask<StubScheduled>::value);
}

TEST(IsScheduledTask, Negative_RejectsUnrelated)
{
    EXPECT_FALSE(IsScheduledTask<PlainClass>::value);
}

TEST(IsBackgroundTask, Positive_DetectsSubclass)
{
    EXPECT_TRUE(IsBackgroundTask<StubBackground>::value);
}

TEST(IsBackgroundTask, Negative_RejectsUnrelated)
{
    EXPECT_FALSE(IsBackgroundTask<PlainClass>::value);
}
