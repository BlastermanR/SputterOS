/**
 * @file test_SystemBuilder.cpp
 * @brief Unit tests for SystemBuilder task dependency validation.
 *
 * Verifies that `SystemBuilder::build()` calls `validateDependencies()`
 * on all registered tasks and rejects the build when any task reports
 * unmet dependencies.
 *
 * Each test uses a unique `DepTestCfg<N>` type. Because `System<Cfg>`
 * holds all state as `inline static` members, each distinct `Cfg` type
 * produces an independent set of statics. Tests are therefore fully
 * isolated with no shared state, and no reset mechanism is needed.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/9/2026
 */

#include "MockSafetyMonitor.h"
#include "MockStreamReader.h"
#include "MockUserApplication.h"
#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/hal/base/ISputterDevice.h"
#include "sputteros/osal/tasks/IScheduledTask.h"
#include <array>
#include <gtest/gtest.h>

using namespace SputterOS;
using ::testing::NiceMock;
using ::testing::Return;

// ===========================================================================
// Per-test isolated configuration
//
// Each test aliases one of these as its `Cfg`. System<DepTestCfg<N>> carries
// independent inline-static state for each distinct N, so no test can
// interfere with another regardless of execution order.
// ===========================================================================

template <int N> struct DepTestCfg
{
    enum class State : uint8_t
    {
        IDLE = 0
    };
    enum class CmdID : uint8_t
    {
        NONE = 0
    };
    struct Command
    {
        CmdID   id;
        uint8_t targetDevice;
        float   value;
    };
    static constexpr int         kMaxCommandsPerTick = 8;
    static constexpr uint8_t     kMaxValidCommandID  = 0;
    static constexpr std::size_t kCoreCount          = 1;
    static constexpr std::size_t kQueueCapacity      = 16;
};

// ===========================================================================
// Test doubles
// ===========================================================================

/**
 * @brief Minimal ISputterDevice stub for dependency registration.
 */
class StubDevice : public ISputterDevice
{
  public:
    bool isHealthy() const override { return true; }
};

/**
 * @brief A user task whose validateDependencies always passes.
 */
class SatisfiedTask : public IScheduledTask
{
  public:
    void          init() override {}
    void          tick(SputterMicros) override {}
    SputterMicros periodUs() const override { return 10000; }
    bool          validateDependencies() const override { return true; }
};

/**
 * @brief A user task whose validateDependencies always fails.
 */
class UnsatisfiedTask : public IScheduledTask
{
  public:
    void          init() override {}
    void          tick(SputterMicros) override {}
    SputterMicros periodUs() const override { return 10000; }
    bool          validateDependencies() const override { return false; }
};

/**
 * @brief A user task that requires at least one registered device.
 */
class DeviceDependentTask : public IScheduledTask
{
  public:
    void          init() override {}
    void          tick(SputterMicros) override {}
    SputterMicros periodUs() const override { return 10000; }
    bool          validateDependencies() const override { return deviceCount() >= 1; }
};

// ===========================================================================
// Tests — each uses a unique DepTestCfg<N> for isolated System<> statics
// ===========================================================================

TEST(SystemBuilderDep, Build_SucceedsWithSatisfiedTask)
{
    using Cfg = DepTestCfg<1>;
    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    SatisfiedTask      userTask;
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    builder.core(0).addScheduledTask(&userTask);

    BuildResult result = builder.build();
    EXPECT_TRUE(result.ok) << result.error;
}

TEST(SystemBuilderDep, Build_FailsWithUnsatisfiedTask)
{
    using Cfg = DepTestCfg<2>;
    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    UnsatisfiedTask    userTask;
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    builder.core(0).addScheduledTask(&userTask);

    BuildResult result = builder.build();
    EXPECT_FALSE(result.ok);
    EXPECT_STREQ(result.error, "Task dependency validation failed");
}

TEST(SystemBuilderDep, Build_FailsWhenDeviceMissing)
{
    using Cfg = DepTestCfg<3>;
    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    DeviceDependentTask userTask; // no addDevice() call → validateDependencies() fails
    SystemBuilder<Cfg>  builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    builder.core(0).addScheduledTask(&userTask);

    BuildResult result = builder.build();
    EXPECT_FALSE(result.ok);
    EXPECT_STREQ(result.error, "Task dependency validation failed");
}

TEST(SystemBuilderDep, Build_SucceedsWhenDeviceProvided)
{
    using Cfg = DepTestCfg<4>;
    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    DeviceDependentTask userTask;
    StubDevice          dev;
    userTask.addDevice(&dev);
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    builder.core(0).addScheduledTask(&userTask);

    BuildResult result = builder.build();
    EXPECT_TRUE(result.ok) << result.error;
}

TEST(SystemBuilderDep, Build_FailsIfAnyTaskUnsatisfied)
{
    using Cfg = DepTestCfg<5>;
    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    SatisfiedTask      goodTask;
    UnsatisfiedTask    badTask;
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    builder.core(0).addScheduledTask(&goodTask);
    builder.core(0).addScheduledTask(&badTask);

    BuildResult result = builder.build();
    EXPECT_FALSE(result.ok);
    EXPECT_STREQ(result.error, "Task dependency validation failed");
}

TEST(SystemBuilderDep, Build_InfraOnlyMode_NoAppNoValidation)
{
    using Cfg = DepTestCfg<6>;
    SatisfiedTask      userTask;
    SystemBuilder<Cfg> builder;
    builder.core(0).addScheduledTask(&userTask);

    BuildResult result = builder.build();
    EXPECT_TRUE(result.ok) << result.error;
}

TEST(SystemBuilderDep, Build_KernelTasksPassValidation)
{
    using Cfg = DepTestCfg<7>;
    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);

    BuildResult result = builder.build();
    EXPECT_TRUE(result.ok) << result.error;
}

// ===========================================================================
// v0.2.0 — Additional coverage for System.h and SystemBuilder.h
// ===========================================================================

TEST(SystemBuilderDep, Build_NoTasksNoApp_Fails)
{
    using Cfg = DepTestCfg<8>;
    SystemBuilder<Cfg> builder;
    // No app, no tasks → "No tasks registered on any core".
    BuildResult result = builder.build();
    EXPECT_FALSE(result.ok);
    EXPECT_STREQ(result.error, "No tasks registered on any core");
}

TEST(SystemBuilderDep, Build_DuplicateBuild_Fails)
{
    using Cfg = DepTestCfg<9>;
    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);

    BuildResult first = builder.build();
    EXPECT_TRUE(first.ok) << first.error;

    BuildResult second = builder.build();
    EXPECT_FALSE(second.ok);
    EXPECT_STREQ(second.error, "SystemBuilder::build() already called");
}

TEST(SystemBuilderDep, Build_SetsIsBuiltFlag)
{
    using Cfg = DepTestCfg<10>;
    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    EXPECT_FALSE(System<Cfg>::isBuilt());

    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    (void)builder.build();

    EXPECT_TRUE(System<Cfg>::isBuilt());
}

TEST(SystemBuilderDep, Build_PopulatesTaskCountOnCore0)
{
    using Cfg = DepTestCfg<11>;
    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    SatisfiedTask      userTask;
    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    builder.core(0).addScheduledTask(&userTask);

    BuildResult result = builder.build();
    ASSERT_TRUE(result.ok) << result.error;

    // 3 kernel tasks + 1 user task on core 0 (single-core config).
    EXPECT_EQ(System<Cfg>::taskCount(0), 4u);
}

static uint64_t g_clockValue = 42000;
static uint64_t testClockSource() { return g_clockValue; }

TEST(SystemBuilderDep, Build_PropagatesClockSource)
{
    using Cfg = DepTestCfg<12>;
    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    builder.setClockSource(testClockSource);

    BuildResult result = builder.build();
    ASSERT_TRUE(result.ok) << result.error;

    EXPECT_TRUE(System<Cfg>::timer().hasClockSource());
    EXPECT_EQ(System<Cfg>::timer().nowMicros(), 42000u);
}

TEST(SystemBuilderDep, Build_ErrorLoggerAccessiblePreAndPostBuild)
{
    using Cfg = DepTestCfg<13>;

    // Accessible before build.
    ErrorLogger &logger = System<Cfg>::errorLogger();
    EXPECT_EQ(logger.count(), 0u);

    NiceMock<MockUserApplication<Cfg>> app;
    NiceMock<MockSafetyMonitor>        monitor;
    NiceMock<MockStreamReader>         stream;
    ON_CALL(monitor, isSafe()).WillByDefault(Return(true));
    std::array<ISafetyMonitor *, 1> monitors = {&monitor};

    SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
    builder.setStream(&stream);
    (void)builder.build();

    // Still accessible after build.
    EXPECT_EQ(System<Cfg>::errorLogger().count(), 0u);
}
