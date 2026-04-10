/**
 * @file test_MultiCoreSync.cpp
 * @brief Unit tests for MultiCoreSync<N> lifecycle barriers.
 *
 * Tests single-core and dual-core scenarios, timeout handling,
 * error propagation, and ICoreErrorHandler callbacks.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/6/2026
 */

#include "sputteros/osal/sync/ICoreErrorHandler.h"
#include "sputteros/osal/sync/MultiCoreSync.h"
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

using ms = std::chrono::milliseconds;

namespace
{

// Mock error handler for testing callbacks
class MockErrorHandler : public SputterOS::ICoreErrorHandler
{
  public:
    void onCoreError(std::size_t coreId, const char *reason) override
    {
        m_lastCoreId = static_cast<int>(coreId);
        m_lastReason = reason;
        m_callCount++;
    }

    int         m_lastCoreId = -1;
    std::string m_lastReason;
    int         m_callCount = 0;
};

// Test fixture for MultiCoreSync tests
class MultiCoreSyncTest : public ::testing::Test
{
  protected:
    void SetUp() override { m_errorHandler = std::make_unique<MockErrorHandler>(); }

    std::unique_ptr<MockErrorHandler> m_errorHandler;
};

} // namespace

// ── Dual-Core Tests ──────────────────────────────────────────────────────────

TEST_F(MultiCoreSyncTest, DualCore_StartupSuccess)
{
    SputterOS::MultiCoreSync<2> sync(m_errorHandler.get());

    std::atomic<bool> core1Ready{false};

    // Core 1 thread
    std::thread core1(
        [&]()
        {
            sync.setInit(1);
            core1Ready = true;

            // Wait for Core 0 to be ready
            EXPECT_TRUE(sync.startupBarrier(1, ms{1000}));
        });

    // Core 0
    sync.setInit(0);

    // Wait for Core 1 to set init
    while (!core1Ready.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(sync.startupBarrier(0, ms{1000}));
    EXPECT_FALSE(sync.anyError());

    core1.join();
}

TEST_F(MultiCoreSyncTest, DualCore_StartupTimeout)
{
    SputterOS::MultiCoreSync<2> sync(m_errorHandler.get());

    // Core 0 sets init and waits
    sync.setInit(0);

    // Core 1 never starts - timeout expected
    EXPECT_FALSE(sync.startupBarrier(0, ms{10})); // 10ms timeout
    EXPECT_TRUE(sync.anyError());
}

TEST_F(MultiCoreSyncTest, DualCore_ErrorPropagation)
{
    SputterOS::MultiCoreSync<2> sync(m_errorHandler.get());

    std::atomic<bool> core1Started{false};

    // Core 1 thread
    std::thread core1(
        [&]()
        {
            sync.setInit(1);
            core1Started = true;

            // Wait indefinitely - should be interrupted by error
            sync.startupBarrier(1, ms{10000});
        });

    // Core 0
    sync.setInit(0);

    // Wait for Core 1 to start
    while (!core1Started.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Core 0 sets error
    sync.setError(0, "Core 0 failure");

    core1.join();

    EXPECT_TRUE(sync.anyError());
    EXPECT_EQ(m_errorHandler->m_callCount, 2);  // Core 0 error + Core 1 timeout
    EXPECT_EQ(m_errorHandler->m_lastCoreId, 1); // Last call from Core 1
}

TEST_F(MultiCoreSyncTest, DualCore_ShutdownSuccess)
{
    SputterOS::MultiCoreSync<2> sync(m_errorHandler.get());

    std::atomic<bool> core1Ready{false};

    // Both cores start up
    sync.setInit(0);
    sync.setInit(1);

    // Core 1 thread for startup
    std::thread core1(
        [&]()
        {
            EXPECT_TRUE(sync.startupBarrier(1, ms{1000}));
            core1Ready = true;
        });

    EXPECT_TRUE(sync.startupBarrier(0, ms{1000}));

    core1.join();
    EXPECT_TRUE(core1Ready);

    // Now shutdown
    std::atomic<bool> core1Shutdown{false};

    // Core 1 thread for shutdown
    std::thread core1ShutdownThread(
        [&]()
        {
            sync.setShutdown(1);
            EXPECT_TRUE(sync.shutdownBarrier(1, ms{1000}));
            core1Shutdown = true;
        });

    // Core 0 shutdown
    sync.setShutdown(0);
    EXPECT_TRUE(sync.shutdownBarrier(0, ms{1000}));

    core1ShutdownThread.join();
    EXPECT_TRUE(core1Shutdown);
    EXPECT_FALSE(sync.anyError());
}

// ── Edge Cases ───────────────────────────────────────────────────────────────

TEST_F(MultiCoreSyncTest, NoErrorHandler)
{
    SputterOS::MultiCoreSync<2> sync(nullptr); // No error handler

    sync.setInit(0);
    sync.setError(0, "Test error");

    // Should not crash, just set error state
    EXPECT_TRUE(sync.anyError());
}

// TEST_F(MultiCoreSyncTest, WaitForCore)
// {
//     SputterOS::MultiCoreSync<2> sync(m_errorHandler.get());

//     std::atomic<bool> core1Waiting{false};

//     // Core 1 waits for Core 0 to be READY
//     std::thread core1([&]() {
//         sync.setInit(1);
//         core1Waiting = true;

//         // Wait for Core 0 to reach READY
//         EXPECT_TRUE(sync.waitForCore(0, SputterOS::CoreState::READY, 1000));
//     });

//     // Core 0
//     sync.setInit(0);

//     // Wait for Core 1 to start waiting
//     while (!core1Waiting.load()) {
//         std::this_thread::sleep_for(std::chrono::milliseconds(1));
//     }

//     // Now Core 0 goes to READY by calling startupBarrier
//     EXPECT_TRUE(sync.startupBarrier(0, 1000));

//     core1.join();
//     EXPECT_FALSE(sync.anyError());
// }