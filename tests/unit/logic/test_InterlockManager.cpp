/**
 * @file test_InterlockManager.cpp
 * @brief Unit tests for InterlockManager.
 *
 * Verifies condition registration, safety evaluation, fault latching, and
 * fault clearing behaviour using mock interlock conditions and fault responses.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/6/2026
 */

#include "MockFaultResponse.h"
#include "MockInterlockCondition.h"
#include "sputteros/logic/InterlockManager.h"
#include <gtest/gtest.h>

using namespace SputterOS;
using ::testing::NiceMock;
using ::testing::Return;

// ===========================================================================
// Fixture
// ===========================================================================

class InterlockManagerTest : public ::testing::Test
{
  protected:
    NiceMock<MockInterlockCondition> condition1;
    NiceMock<MockInterlockCondition> condition2;
    NiceMock<MockFaultResponse>      faultResponse;

    /**
     * @brief Configure an InterlockManager with condition1 registered and
     *        faultResponse set.
     */
    void configureIM(InterlockManager &im)
    {
        im.registerCondition(&condition1);
        im.setFaultResponse(&faultResponse);
    }
};

// ---------------------------------------------------------------------------
// registerCondition
// ---------------------------------------------------------------------------

TEST_F(InterlockManagerTest, RegisterCondition_ValidPointer_ReturnsOK)
{
    InterlockManager im;
    EXPECT_EQ(im.registerCondition(&condition1), OpResult::OK);
}

TEST_F(InterlockManagerTest, RegisterCondition_NullPointer_ReturnsNullArg)
{
    InterlockManager im;
    EXPECT_EQ(im.registerCondition(nullptr), OpResult::NULL_ARG);
}

TEST_F(InterlockManagerTest, RegisterCondition_ExceedsMax_ReturnsFull)
{
    InterlockManager                 im;
    NiceMock<MockInterlockCondition> extras[InterlockManager::kMaxConditions];

    for (std::size_t i = 0; i < InterlockManager::kMaxConditions; ++i)
    {
        EXPECT_EQ(im.registerCondition(&extras[i]), OpResult::OK);
    }
    // One more should fail
    EXPECT_EQ(im.registerCondition(&condition1), OpResult::FULL);
}

// ---------------------------------------------------------------------------
// validateDependencies
// ---------------------------------------------------------------------------

TEST_F(InterlockManagerTest, ValidateDependencies_ConditionAndResponse_ReturnsTrue)
{
    InterlockManager im;
    configureIM(im);
    EXPECT_TRUE(im.validateDependencies());
}

TEST_F(InterlockManagerTest, ValidateDependencies_NoConditions_ReturnsFalse)
{
    InterlockManager im;
    im.setFaultResponse(&faultResponse);
    EXPECT_FALSE(im.validateDependencies());
}

TEST_F(InterlockManagerTest, ValidateDependencies_NoResponse_ReturnsFalse)
{
    InterlockManager im;
    im.registerCondition(&condition1);
    EXPECT_FALSE(im.validateDependencies());
}

// ---------------------------------------------------------------------------
// checkAllInterlocks
// ---------------------------------------------------------------------------

TEST_F(InterlockManagerTest, CheckAllInterlocks_AllSafe_ReturnsTrue)
{
    ON_CALL(condition1, isSafe()).WillByDefault(Return(true));
    InterlockManager im;
    configureIM(im);
    EXPECT_TRUE(im.checkAllInterlocks());
}

TEST_F(InterlockManagerTest, CheckAllInterlocks_OneUnsafe_ReturnsFalse)
{
    ON_CALL(condition1, isSafe()).WillByDefault(Return(false));
    InterlockManager im;
    configureIM(im);
    EXPECT_FALSE(im.checkAllInterlocks());
}

TEST_F(InterlockManagerTest, CheckAllInterlocks_MultipleConditions_FirstUnsafe_ReturnsFalse)
{
    ON_CALL(condition1, isSafe()).WillByDefault(Return(true));
    ON_CALL(condition2, isSafe()).WillByDefault(Return(false));

    InterlockManager im;
    im.registerCondition(&condition1);
    im.registerCondition(&condition2);
    im.setFaultResponse(&faultResponse);

    EXPECT_FALSE(im.checkAllInterlocks());
}

TEST_F(InterlockManagerTest, CheckAllInterlocks_ZeroConditions_ReturnsTrue)
{
    InterlockManager im;
    // No conditions registered — vacuously true
    EXPECT_TRUE(im.checkAllInterlocks());
}

// ---------------------------------------------------------------------------
// triggerSoftAbort / hasSoftAbort
// ---------------------------------------------------------------------------

TEST_F(InterlockManagerTest, SoftAbort_InitiallyFalse)
{
    InterlockManager im;
    configureIM(im);
    EXPECT_FALSE(im.hasSoftAbort());
}

TEST_F(InterlockManagerTest, SoftAbort_TrueAfterTrigger)
{
    InterlockManager im;
    configureIM(im);
    im.triggerSoftAbort();
    EXPECT_TRUE(im.hasSoftAbort());
}

// ---------------------------------------------------------------------------
// triggerHardFault / isHardFaulted
// ---------------------------------------------------------------------------

TEST_F(InterlockManagerTest, HardFault_InitiallyFalse)
{
    InterlockManager im;
    configureIM(im);
    EXPECT_FALSE(im.isHardFaulted());
}

TEST_F(InterlockManagerTest, HardFault_TrueAfterTrigger)
{
    InterlockManager im;
    configureIM(im);
    im.triggerHardFault();
    EXPECT_TRUE(im.isHardFaulted());
}

TEST_F(InterlockManagerTest, HardFault_ExecutesFaultResponse)
{
    InterlockManager im;
    configureIM(im);
    EXPECT_CALL(faultResponse, execute()).Times(1);
    im.triggerHardFault();
}

TEST_F(InterlockManagerTest, HardFault_NullResponseDoesNotCrash)
{
    InterlockManager im;
    im.registerCondition(&condition1);
    // No fault response set
    EXPECT_NO_FATAL_FAILURE(im.triggerHardFault());
    EXPECT_TRUE(im.isHardFaulted());
}

// ---------------------------------------------------------------------------
// clearFault
// ---------------------------------------------------------------------------

TEST_F(InterlockManagerTest, ClearFault_NominalConditions_ReturnsTrueAndClearsFlags)
{
    ON_CALL(condition1, isSafe()).WillByDefault(Return(true));
    InterlockManager im;
    configureIM(im);
    im.triggerSoftAbort();
    im.triggerHardFault();

    EXPECT_TRUE(im.clearFault());
    EXPECT_FALSE(im.hasSoftAbort());
    EXPECT_FALSE(im.isHardFaulted());
}

TEST_F(InterlockManagerTest, ClearFault_WhileConditionViolated_ReturnsFalse)
{
    ON_CALL(condition1, isSafe()).WillByDefault(Return(false));
    InterlockManager im;
    configureIM(im);
    im.triggerHardFault();

    EXPECT_FALSE(im.clearFault());
    EXPECT_TRUE(im.isHardFaulted());
}

TEST_F(InterlockManagerTest, ClearFault_WhenNoFaultPresent_ReturnsTrueCleanly)
{
    ON_CALL(condition1, isSafe()).WillByDefault(Return(true));
    InterlockManager im;
    configureIM(im);
    EXPECT_TRUE(im.clearFault());
}
