/**
 * @file TelemetryLogger.cpp
 * @brief Implementation of the task-aware telemetry log buffer.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/6/2026
 */

#include "sputteros/utils/logging/TelemetryLogger.h"
#include "sputteros/utils/logging/LightweightStringBuilder.h"

#include "sputteros/utils/MemUtils.h"

namespace SputterOS
{

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TelemetryLogger::TelemetryLogger(IMutex *guard)
    : m_guard(guard), m_head(0), m_tail(0), m_count(0), m_verbosity(Verbosity::STATUS)
{
    // Intentionally Empty
}

// ---------------------------------------------------------------------------
// Write API
// ---------------------------------------------------------------------------

void TelemetryLogger::log(TaskID task, const char *text, Verbosity level, SputterMicros timestamp)
{
    if (m_guard)
        m_guard->lock(0);

    // Overwrite the oldest entry when the buffer is full (black-box behaviour,
    // same contract as ErrorLogger).
    if (m_count == kCapacity)
    {
        m_tail = (m_tail + 1) % kCapacity;
    }
    else
    {
        ++m_count;
    }

    Entry &e    = m_buf[m_head];
    e.timestamp = timestamp;
    e.task      = task;
    e.level     = level;

    sput_strncpy(e.text, text, kTextLen - 1);
    e.text[kTextLen - 1] = '\0';

    m_head = (m_head + 1) % kCapacity;

    if (m_guard)
        m_guard->unlock();
}

// ---------------------------------------------------------------------------
// Drain API
// ---------------------------------------------------------------------------

void TelemetryLogger::drain(DrainWriteFn writeFn, void *ctx)
{
    if (!writeFn)
    {
        return;
    }

    if (m_guard)
        m_guard->lock(0);

    LightweightStringBuilder sb;

    while (m_count > 0)
    {
        const Entry &e = m_buf[m_tail];
        m_tail         = (m_tail + 1) % kCapacity;
        --m_count;

        // Discard entries above the current verbosity threshold.
        if (e.level > m_verbosity)
        {
            continue;
        }

        // Format: [<timestamp_ms>][<TaskName>] <text>\n
        sb.clear();
        sb.append('[')
            .append(static_cast<uint32_t>(e.timestamp / 1000))
            .append(']')
            .append('[')
            .append(taskName(e.task))
            .append(']')
            .append(' ')
            .append(e.text)
            .append('\n');

        writeFn(reinterpret_cast<const uint8_t *>(sb.c_str()), sb.length(), ctx);
    }

    if (m_guard)
        m_guard->unlock();
}

// ---------------------------------------------------------------------------
// Verbosity control
// ---------------------------------------------------------------------------

void TelemetryLogger::setVerbosity(Verbosity level)
{
    if (m_guard)
        m_guard->lock(0);
    m_verbosity = level;
    if (m_guard)
        m_guard->unlock();
}

TelemetryLogger::Verbosity TelemetryLogger::getVerbosity() const { return m_verbosity; }

// ---------------------------------------------------------------------------
// Inspection
// ---------------------------------------------------------------------------

std::size_t TelemetryLogger::count() const
{
    // Non-mutating read; safe without lock in most cases, but lock if available
    // to prevent torn reads on some architectures.
    if (m_guard)
        m_guard->lock(0);
    std::size_t result = m_count;
    if (m_guard)
        m_guard->unlock();
    return result;
}

void TelemetryLogger::clear()
{
    if (m_guard)
        m_guard->lock(0);
    m_head  = 0;
    m_tail  = 0;
    m_count = 0;
    if (m_guard)
        m_guard->unlock();
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

const char *TelemetryLogger::taskName(TaskID task)
{
    switch (task)
    {
    case TaskID::CONTROL:
        return "ControlTask";
    case TaskID::COMMS:
        return "CommsTask";
    case TaskID::DIAGNOSTICS:
        return "DiagnosticsTask";
    case TaskID::SYSTEM:
        return "System";
    default:
        return "Unknown";
    }
}

} // namespace SputterOS
