/**
 * @file test_ITask.cpp
 * @brief Unit tests for ITask device dependency registration and validation.
 *
 * Tests the `addDevice()`, `device()`, `deviceCount()`, and
 * `validateDependencies()` API added to `ITask` for hardware
 * dependency tracking.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/9/2026
 */

#include "sputteros/hal/base/ISputterDevice.h"
#include "sputteros/osal/tasks/ITask.h"
#include <gtest/gtest.h>

using namespace SputterOS;

// ===========================================================================
// Concrete test doubles
// ===========================================================================

/**
 * @brief Minimal ITask implementation for testing base-class device features.
 */
class ConcreteTask : public ITask
{
  public:
    void init() override {}
    void tick(SputterMicros /*systemTimeMicros*/) override {}
};

/**
 * @brief ITask subclass that overrides validateDependencies to require
 *        exactly 2 registered devices.
 */
class TwoDeviceTask : public ITask
{
  public:
    void init() override {}
    void tick(SputterMicros /*systemTimeMicros*/) override {}

    bool validateDependencies() const override { return deviceCount() >= 2; }
};

/**
 * @brief Minimal ISputterDevice stub for registering as a dependency.
 */
class StubDevice : public ISputterDevice
{
  public:
    bool isHealthy() const override { return true; }
};

// ===========================================================================
// Device Registration
// ===========================================================================

TEST(ITaskDeviceTest, DeviceCount_InitiallyZero)
{
    ConcreteTask task;
    EXPECT_EQ(task.deviceCount(), 0u);
}

TEST(ITaskDeviceTest, AddDevice_IncreasesCount)
{
    ConcreteTask task;
    StubDevice   dev;
    EXPECT_EQ(task.addDevice(&dev), OpResult::OK);
    EXPECT_EQ(task.deviceCount(), 1u);
}

TEST(ITaskDeviceTest, AddDevice_RejectsNull)
{
    ConcreteTask task;
    EXPECT_EQ(task.addDevice(nullptr), OpResult::NULL_ARG);
    EXPECT_EQ(task.deviceCount(), 0u);
}

TEST(ITaskDeviceTest, AddDevice_StoresPointer)
{
    ConcreteTask task;
    StubDevice   dev;
    task.addDevice(&dev);
    EXPECT_EQ(task.device(0), &dev);
}

TEST(ITaskDeviceTest, AddDevice_MultipleDevices)
{
    ConcreteTask task;
    StubDevice   d1, d2, d3;
    task.addDevice(&d1);
    task.addDevice(&d2);
    task.addDevice(&d3);

    EXPECT_EQ(task.deviceCount(), 3u);
    EXPECT_EQ(task.device(0), &d1);
    EXPECT_EQ(task.device(1), &d2);
    EXPECT_EQ(task.device(2), &d3);
}

TEST(ITaskDeviceTest, AddDevice_RejectsWhenFull)
{
    ConcreteTask task;
    StubDevice   devs[ITask::kMaxDevices];

    for (std::size_t i = 0; i < ITask::kMaxDevices; ++i)
    {
        EXPECT_EQ(task.addDevice(&devs[i]), OpResult::OK);
    }
    EXPECT_EQ(task.deviceCount(), ITask::kMaxDevices);

    // One more should fail
    StubDevice overflow;
    EXPECT_EQ(task.addDevice(&overflow), OpResult::FULL);
    EXPECT_EQ(task.deviceCount(), ITask::kMaxDevices);
}

TEST(ITaskDeviceTest, Device_ReturnsNullForOutOfRange)
{
    ConcreteTask task;
    EXPECT_EQ(task.device(0), nullptr);
    EXPECT_EQ(task.device(100), nullptr);
}

// ===========================================================================
// validateDependencies — default
// ===========================================================================

TEST(ITaskDeviceTest, ValidateDependencies_DefaultReturnsTrue)
{
    ConcreteTask task;
    EXPECT_TRUE(task.validateDependencies());
}

TEST(ITaskDeviceTest, ValidateDependencies_DefaultReturnsTrueEvenWithDevices)
{
    ConcreteTask task;
    StubDevice   dev;
    task.addDevice(&dev);
    EXPECT_TRUE(task.validateDependencies());
}

// ===========================================================================
// validateDependencies — overridden
// ===========================================================================

TEST(ITaskDeviceTest, ValidateDependencies_Override_FailsWhenMissing)
{
    TwoDeviceTask task;
    EXPECT_FALSE(task.validateDependencies());

    StubDevice d1;
    task.addDevice(&d1);
    EXPECT_FALSE(task.validateDependencies()); // still only 1
}

TEST(ITaskDeviceTest, ValidateDependencies_Override_PassesWhenMet)
{
    TwoDeviceTask task;
    StubDevice    d1, d2;
    task.addDevice(&d1);
    task.addDevice(&d2);
    EXPECT_TRUE(task.validateDependencies());
}

TEST(ITaskDeviceTest, ValidateDependencies_Override_PassesWhenExceeded)
{
    TwoDeviceTask task;
    StubDevice    d1, d2, d3;
    task.addDevice(&d1);
    task.addDevice(&d2);
    task.addDevice(&d3);
    EXPECT_TRUE(task.validateDependencies());
}
