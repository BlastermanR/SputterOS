/**
 * @file test_ErrorLoggingPipeline.cpp
 * @brief System tests for ErrorLogger, TelemetryLogger, and MemoryProfiler.
 *
 * Validates the diagnostic infrastructure through the real System<Cfg>
 * singleton and as standalone objects:
 * - ErrorLogger FIFO ordering: entries are read back in insertion order.
 * - ErrorLogger circular overflow: the oldest entry is overwritten when the
 *   buffer is full; the newest entries are retained and readable.
 * - MemoryProfiler is accessible via System<Cfg>::memProfiler() after a
 *   build and update() does not crash in the host test environment.
 * - TelemetryLogger (standalone): log() increases count, drain() clears it.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "SystemTestConfig.h"
#include "SystemTestHelpers.h"

#include "sputteros/builder/SystemBuilder.h"
#include "sputteros/kernel/System.h"
#include "sputteros/utils/MemoryProfiler.h"
#include "sputteros/utils/logging/ErrorLogger.h"
#include "sputteros/utils/logging/TelemetryLogger.h"

#include "../../unit/mocks/KernelTestAccess.h"

#include <gtest/gtest.h>

using namespace SputterOS;
using namespace SputterOS::SystemTests;

using Cfg = SingleCoreConfig;

// =========================================================================
// Fixture
// =========================================================================

class ErrorLoggingPipeline : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        InstrumentedApp<Cfg>    app;
        FakeStreamReader        stream;
        AlwaysSafeSafetyMonitor monitor;
        ISafetyMonitor         *monitors[] = {&monitor};

        SystemBuilder<Cfg> builder(&app, monitors, 1);
        builder.setStream(&stream).setWatchdogKick(nullptr);
        ASSERT_TRUE(builder.build());

        System<Cfg>::init(0);
    }

    void TearDown() override { Kernel::KernelTestAccess::resetSystem<Cfg>(); }
};

// =========================================================================
// Tests
// =========================================================================

TEST_F(ErrorLoggingPipeline, ErrorLoggerFIFOOrdering)
{
    auto &logger = System<Cfg>::errorLogger();

    logger.log(ErrorLogger::ErrorCode::STATE_TRANSITION, SputterMicros(100), 1.0f);
    logger.log(ErrorLogger::ErrorCode::INTERLOCK_TRIP,   SputterMicros(200), 2.0f);
    logger.log(ErrorLogger::ErrorCode::SOFT_ABORT,       SputterMicros(300), 3.0f);

    ASSERT_EQ(logger.count(), 3u);

    ErrorLogger::Entry e{};
    ASSERT_TRUE(logger.read(e));
    EXPECT_EQ(e.code, ErrorLogger::ErrorCode::STATE_TRANSITION);
    EXPECT_EQ(e.timestamp, SputterMicros(100));

    ASSERT_TRUE(logger.read(e));
    EXPECT_EQ(e.code, ErrorLogger::ErrorCode::INTERLOCK_TRIP);
    EXPECT_EQ(e.timestamp, SputterMicros(200));

    ASSERT_TRUE(logger.read(e));
    EXPECT_EQ(e.code, ErrorLogger::ErrorCode::SOFT_ABORT);
    EXPECT_EQ(e.timestamp, SputterMicros(300));

    EXPECT_FALSE(logger.read(e)); // Buffer should now be empty.
}

TEST_F(ErrorLoggingPipeline, ErrorLoggerCircularOverwrite)
{
    // The ErrorLogger holds kCapacity=32 entries. Logging 33 entries must
    // overwrite the oldest (timestamp=0) and retain the 32 newest.
    auto &logger = System<Cfg>::errorLogger();

    static constexpr std::size_t kOverflow = 33;

    for (std::size_t i = 0; i < kOverflow; ++i)
    {
        logger.log(ErrorLogger::ErrorCode::SENSOR_ERROR,
                   SputterMicros(static_cast<uint64_t>(i)),
                   static_cast<float>(i));
    }

    // Drain all readable entries and collect timestamps.
    bool             foundTimestamp0  = false;
    bool             foundTimestamp32 = false;
    std::size_t      readCount        = 0;
    ErrorLogger::Entry e{};
    while (logger.read(e))
    {
        if (e.timestamp == SputterMicros(0))
            foundTimestamp0 = true;
        if (e.timestamp == SputterMicros(32))
            foundTimestamp32 = true;
        ++readCount;
    }

    // Must have overwritten the oldest entry (timestamp 0).
    EXPECT_FALSE(foundTimestamp0) << "Oldest entry should have been overwritten";
    // The newest entry (timestamp 32) must be present.
    EXPECT_TRUE(foundTimestamp32) << "Newest entry must be readable after overflow";
    // Exactly kCapacity entries must be readable.
    EXPECT_EQ(readCount, 32u);
}

TEST_F(ErrorLoggingPipeline, MemoryProfilerAccessibleAfterBuild)
{
    // memProfiler() must return a valid reference and update() must not crash.
    auto &profiler = System<Cfg>::memProfiler();

    EXPECT_NO_FATAL_FAILURE({ profiler.update(); });

    // Static accessors must return without crashing (values are platform-specific).
    EXPECT_NO_FATAL_FAILURE({
        (void)MemoryProfiler::getFreeHeapBytes();
        (void)MemoryProfiler::getStackHighWaterMark();
    });
}

TEST_F(ErrorLoggingPipeline, TelemetryLoggerLogAndDrain)
{
    // TelemetryLogger is a standalone utility; test its log/drain lifecycle.
    TelemetryLogger telemetry;

    EXPECT_EQ(telemetry.count(), 0u);

    telemetry.log(TelemetryLogger::TaskID::CONTROL,
                  "Deposition started",
                  TelemetryLogger::Verbosity::STATUS,
                  SputterMicros(1000));

    telemetry.log(TelemetryLogger::TaskID::DIAGNOSTICS,
                  "Watchdog kicked",
                  TelemetryLogger::Verbosity::INFO,
                  SputterMicros(2000));

    EXPECT_EQ(telemetry.count(), 2u);

    // Drain to a null sink — just verify it completes without crashing.
    EXPECT_NO_FATAL_FAILURE({
        telemetry.drain([](const uint8_t *, std::size_t, void *) {}, nullptr);
    });

    EXPECT_EQ(telemetry.count(), 0u);
}
