#ifndef SPUTTEROS_SYSTEMTESTS_HELPERS_H
#define SPUTTEROS_SYSTEMTESTS_HELPERS_H

/**
 * @file SystemTestHelpers.h
 * @brief Test doubles for system integration tests.
 *
 * Provides lightweight stubs that satisfy SputterOS interfaces without
 * requiring real hardware. All state is directly inspectable by test code.
 */

#include "sputteros/hal/devices/IStream.h"
#include "sputteros/kernel/interfaces/ISafetyMonitor.h"
#include "sputteros/kernel/interfaces/IUserApplication.h"
#include "sputteros/logic/IFaultResponse.h"
#include "sputteros/logic/IInterlockCondition.h"
#include "sputteros/osal/SputterTime.h"
#include "sputteros/osal/tasks/ITask.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace SputterOS
{
namespace SystemTests
{

// =========================================================================
// InstrumentedTask — records init/tick calls for verification
// =========================================================================

class InstrumentedTask : public ITask
{
  public:
    void init() override { ++initCount; }

    void tick(SputterMicros systemTimeMicros) override
    {
        ++tickCount;
        lastTickTime = systemTimeMicros;
    }

    uint32_t      initCount    = 0;
    uint32_t      tickCount    = 0;
    SputterMicros lastTickTime = 0;
};

// =========================================================================
// FakeStreamReader — stub stream returning zero bytes
// =========================================================================

class FakeStreamReader : public IStream
{
  public:
    FakeStreamReader()                                    = default;
    FakeStreamReader(const FakeStreamReader &)            = delete;
    FakeStreamReader &operator=(const FakeStreamReader &) = delete;

    std::size_t available() const override { return 0; }
    std::size_t read(uint8_t *, std::size_t) override { return 0; }
    std::size_t write(const uint8_t *, std::size_t len) override { return len; }
    bool        isConnected() const override { return true; }
};

// =========================================================================
// AlwaysSafeSafetyMonitor — never trips
// =========================================================================

class AlwaysSafeSafetyMonitor : public ISafetyMonitor
{
  public:
    bool isSafe() const override { return true; }
};

// =========================================================================
// InstrumentedApp — records kernel interactions
// =========================================================================

template <typename Cfg> class InstrumentedApp : public IUserApplication<Cfg>
{
  public:
    using CommandStruct = typename Cfg::Command;

    void init() override { ++initCount; }

    void tick(SputterMicros systemTimeMicros) override
    {
        ++tickCount;
        lastTickTime = systemTimeMicros;
    }

    void handleCommand(const CommandStruct &cmd) override
    {
        if (commandCount < kMaxCommands)
        {
            commands[commandCount++] = cmd;
        }
    }

    void forceSafeAbort() override { ++abortCount; }

    uint32_t      initCount    = 0;
    uint32_t      tickCount    = 0;
    SputterMicros lastTickTime = 0;
    uint32_t      abortCount   = 0;

    static constexpr std::size_t kMaxCommands = 32;
    CommandStruct                commands[kMaxCommands]{};
    std::size_t                  commandCount = 0;
};

// =========================================================================
// ScriptedStream — feeds a pre-loaded byte buffer through IStream
// =========================================================================

/**
 * @brief IStream stub backed by a fixed byte buffer.
 *
 * Load a command string via `loadString()` before the test tick loop.
 * The stream drains sequentially; write() calls are silently discarded.
 */
class ScriptedStream : public IStream
{
  public:
    /** @brief Load bytes from a null-terminated C string (null byte excluded). */
    void loadString(const char *str)
    {
        m_len = 0;
        m_pos = 0;
        while (str && *str && m_len < kCapacity)
        {
            m_buf[m_len++] = static_cast<uint8_t>(*str++);
        }
    }

    std::size_t available() const override { return (m_pos < m_len) ? (m_len - m_pos) : 0u; }

    std::size_t read(uint8_t *buf, std::size_t maxLen) override
    {
        std::size_t n = 0;
        while (n < maxLen && m_pos < m_len)
        {
            buf[n++] = m_buf[m_pos++];
        }
        return n;
    }

    std::size_t write(const uint8_t *, std::size_t len) override { return len; }

    bool isConnected() const override { return true; }

    /** @brief True once all loaded bytes have been consumed. */
    bool isDrained() const { return m_pos >= m_len; }

    static constexpr std::size_t kCapacity = 512;

  private:
    uint8_t     m_buf[kCapacity]{};
    std::size_t m_len = 0;
    std::size_t m_pos = 0;
};

// =========================================================================
// TrippableMonitor — ISafetyMonitor whose safe state is toggled at runtime
// =========================================================================

/**
 * @brief ISafetyMonitor stub with a publicly settable safety flag.
 *
 * Set `safeFlag = false` to simulate a tripped safety condition and
 * observe `ControlTask`'s abort response.
 */
class TrippableMonitor : public ISafetyMonitor
{
  public:
    bool safeFlag = true;

    bool isSafe() const override { return safeFlag; }
};

// =========================================================================
// TrippableCondition — IInterlockCondition whose safe state can be toggled
// =========================================================================

/**
 * @brief IInterlockCondition stub with a publicly settable safety flag.
 *
 * Register with an `InterlockManager` to drive interlock pipeline tests.
 */
class TrippableCondition : public IInterlockCondition
{
  public:
    bool safeFlag = true;

    bool        isSafe() const override { return safeFlag; }
    const char *name() const override { return "TrippableCondition"; }
};

// =========================================================================
// CountingFaultResponse — IFaultResponse that counts execute() invocations
// =========================================================================

/**
 * @brief IFaultResponse stub that counts how many times execute() is called.
 *
 * Pass to `InterlockManager::setFaultResponse()` to verify that the
 * hard-fault latch prevents duplicate execute() calls.
 */
class CountingFaultResponse : public IFaultResponse
{
  public:
    uint32_t executeCount = 0;

    void execute() override { ++executeCount; }
};

} // namespace SystemTests
} // namespace SputterOS

#endif // SPUTTEROS_SYSTEMTESTS_HELPERS_H
