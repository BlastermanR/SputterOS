/**
 * @file PerformanceFormatter.cpp
 * @brief Heap-free formatter implementation for PerformanceSnapshot.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "sputteros/utils/PerformanceFormatter.h"
#include "sputteros/kernel/metrics/TaskTimer.h"

namespace SputterOS
{

// =========================================================================
// Private helpers
// =========================================================================

std::size_t PerformanceFormatter::appendStr(char *buf, std::size_t pos, std::size_t bufLen, const char *str)
{
    std::size_t written = 0;
    while (*str && pos + written + 1 < bufLen)
    {
        buf[pos + written] = *str++;
        ++written;
    }
    return written;
}

std::size_t PerformanceFormatter::appendChar(char *buf, std::size_t pos, std::size_t bufLen, char c)
{
    if (pos + 1 < bufLen)
    {
        buf[pos] = c;
        return 1;
    }
    return 0;
}

std::size_t PerformanceFormatter::appendU64(char *buf, std::size_t pos, std::size_t bufLen, uint64_t val)
{
    // Convert to decimal in a temp buffer, then copy
    char   tmp[21]; // max uint64 is 20 digits
    int    idx = 0;
    if (val == 0)
    {
        tmp[idx++] = '0';
    }
    else
    {
        while (val > 0)
        {
            tmp[idx++] = '0' + static_cast<char>(val % 10);
            val /= 10;
        }
    }

    // Reverse and append
    std::size_t written = 0;
    for (int i = idx - 1; i >= 0; --i)
    {
        if (pos + written + 1 >= bufLen)
            break;
        buf[pos + written] = tmp[i];
        ++written;
    }
    return written;
}

std::size_t PerformanceFormatter::appendU32(char *buf, std::size_t pos, std::size_t bufLen, uint32_t val)
{
    return appendU64(buf, pos, bufLen, static_cast<uint64_t>(val));
}

std::size_t PerformanceFormatter::appendFloat(char *buf, std::size_t pos, std::size_t bufLen, float val)
{
    std::size_t written = 0;

    if (val < 0.0f)
    {
        written += appendChar(buf, pos + written, bufLen, '-');
        val = -val;
    }

    auto intPart = static_cast<uint32_t>(val);
    written += appendU32(buf, pos + written, bufLen, intPart);
    written += appendChar(buf, pos + written, bufLen, '.');

    // 3 decimal places via integer math
    float frac    = val - static_cast<float>(intPart);
    auto  fracInt = static_cast<uint32_t>(frac * 1000.0f + 0.5f);
    if (fracInt >= 1000)
        fracInt = 999;

    // Leading zeros
    if (fracInt < 100)
        written += appendChar(buf, pos + written, bufLen, '0');
    if (fracInt < 10)
        written += appendChar(buf, pos + written, bufLen, '0');

    written += appendU32(buf, pos + written, bufLen, fracInt);
    return written;
}

// =========================================================================
// Key-Value format
// =========================================================================

/// Helper macro to reduce repetition in key=value formatting
#define KV_U64(key, val)                                                               \
    do                                                                                 \
    {                                                                                  \
        pos += appendStr(buf, pos, bufLen, key "=");                                   \
        pos += appendU64(buf, pos, bufLen, static_cast<uint64_t>(val));                \
        pos += appendChar(buf, pos, bufLen, '\n');                                     \
    } while (0)

#define KV_U32(key, val)                                                               \
    do                                                                                 \
    {                                                                                  \
        pos += appendStr(buf, pos, bufLen, key "=");                                   \
        pos += appendU32(buf, pos, bufLen, static_cast<uint32_t>(val));                \
        pos += appendChar(buf, pos, bufLen, '\n');                                     \
    } while (0)

#define KV_FLOAT(key, val)                                                             \
    do                                                                                 \
    {                                                                                  \
        pos += appendStr(buf, pos, bufLen, key "=");                                   \
        pos += appendFloat(buf, pos, bufLen, val);                                     \
        pos += appendChar(buf, pos, bufLen, '\n');                                     \
    } while (0)

std::size_t PerformanceFormatter::formatKeyValue(const PerformanceSnapshot &snap, char *buf, std::size_t bufLen)
{
    if (!buf || bufLen < 2)
        return 0;

    std::size_t pos = 0;

    // System-level metrics
    KV_U64("timestamp", snap.timestamp);
    KV_U32("cores", snap.coreCount);

    for (std::size_t c = 0; c < snap.coreCount && c < kMaxSnapshotCores; ++c)
    {
        pos += appendStr(buf, pos, bufLen, "core");
        pos += appendU32(buf, pos, bufLen, static_cast<uint32_t>(c));
        pos += appendStr(buf, pos, bufLen, "_util=");
        pos += appendFloat(buf, pos, bufLen, snap.coreUtilization[c]);
        pos += appendChar(buf, pos, bufLen, '\n');
    }

    KV_U64("queue_depth", snap.queueDepth);
    KV_U64("queue_max", snap.queueMaxDepth);
    KV_FLOAT("queue_avg", snap.queueAvgDepth);

    KV_U64("heap_peak", snap.peakHeapUsed);
    KV_U64("heap_free", snap.freeHeap);
    KV_U64("stack_hw", snap.stackHighWater);

    KV_U64("gap_total", snap.totalGapUs);
    KV_U64("gap_max", snap.maxGapUs);
    KV_FLOAT("gap_avg", snap.avgGapUs);
    KV_U32("sched_ticks", snap.schedulerTickCount);
    KV_U32("overruns", snap.totalOverruns);
    KV_U32("deadline_misses", snap.totalDeadlineMisses);

    KV_U32("task_count", snap.taskCount);

    // Per-task metrics
    for (std::size_t i = 0; i < snap.taskCount && i < kMaxSnapshotTasks; ++i)
    {
        const auto &t = snap.tasks[i];
        char prefix[8]; // "tNN_" fits comfortably
        std::size_t pLen = 0;
        prefix[pLen++]   = 't';
        if (i >= 10)
            prefix[pLen++] = '0' + static_cast<char>(i / 10);
        prefix[pLen++] = '0' + static_cast<char>(i % 10);
        prefix[pLen++] = '_';
        prefix[pLen]   = '\0';

        pos += appendStr(buf, pos, bufLen, prefix);
        pos += appendStr(buf, pos, bufLen, "core=");
        pos += appendU32(buf, pos, bufLen, static_cast<uint32_t>(t.coreId));
        pos += appendChar(buf, pos, bufLen, '\n');

        pos += appendStr(buf, pos, bufLen, prefix);
        pos += appendStr(buf, pos, bufLen, "last=");
        pos += appendU64(buf, pos, bufLen, t.lastUs);
        pos += appendChar(buf, pos, bufLen, '\n');

        pos += appendStr(buf, pos, bufLen, prefix);
        pos += appendStr(buf, pos, bufLen, "min=");
        pos += appendU64(buf, pos, bufLen, t.minUs);
        pos += appendChar(buf, pos, bufLen, '\n');

        pos += appendStr(buf, pos, bufLen, prefix);
        pos += appendStr(buf, pos, bufLen, "max=");
        pos += appendU64(buf, pos, bufLen, t.maxUs);
        pos += appendChar(buf, pos, bufLen, '\n');

        pos += appendStr(buf, pos, bufLen, prefix);
        pos += appendStr(buf, pos, bufLen, "avg=");
        pos += appendFloat(buf, pos, bufLen, t.avgUs);
        pos += appendChar(buf, pos, bufLen, '\n');

        pos += appendStr(buf, pos, bufLen, prefix);
        pos += appendStr(buf, pos, bufLen, "samples=");
        pos += appendU32(buf, pos, bufLen, t.samples);
        pos += appendChar(buf, pos, bufLen, '\n');

        pos += appendStr(buf, pos, bufLen, prefix);
        pos += appendStr(buf, pos, bufLen, "overruns=");
        pos += appendU32(buf, pos, bufLen, t.overruns);
        pos += appendChar(buf, pos, bufLen, '\n');

        pos += appendStr(buf, pos, bufLen, prefix);
        pos += appendStr(buf, pos, bufLen, "misses=");
        pos += appendU32(buf, pos, bufLen, t.misses);
        pos += appendChar(buf, pos, bufLen, '\n');

        // Histogram as comma-separated values
        pos += appendStr(buf, pos, bufLen, prefix);
        pos += appendStr(buf, pos, bufLen, "hist=");
        for (std::size_t b = 0; b < Kernel::TaskTimer::kHistogramBuckets; ++b)
        {
            if (b > 0)
                pos += appendChar(buf, pos, bufLen, ',');
            pos += appendU32(buf, pos, bufLen, t.histogram[b]);
        }
        pos += appendChar(buf, pos, bufLen, '\n');
    }

    // Null-terminate
    if (pos < bufLen)
        buf[pos] = '\0';
    else if (bufLen > 0)
        buf[bufLen - 1] = '\0';

    return pos;
}

#undef KV_U64
#undef KV_U32
#undef KV_FLOAT

// =========================================================================
// CSV format
// =========================================================================

std::size_t PerformanceFormatter::formatCSV(const PerformanceSnapshot &snap, char *buf, std::size_t bufLen)
{
    if (!buf || bufLen < 2)
        return 0;

    std::size_t pos = 0;

    // Header row for system metrics
    pos += appendStr(buf, pos, bufLen, "timestamp,cores,queue_depth,queue_max,queue_avg,"
                                       "heap_peak,heap_free,stack_hw,"
                                       "gap_total,gap_max,gap_avg,sched_ticks,overruns,deadline_misses\n");

    // System data row
    pos += appendU64(buf, pos, bufLen, snap.timestamp);
    pos += appendChar(buf, pos, bufLen, ',');
    pos += appendU32(buf, pos, bufLen, static_cast<uint32_t>(snap.coreCount));
    pos += appendChar(buf, pos, bufLen, ',');
    pos += appendU64(buf, pos, bufLen, static_cast<uint64_t>(snap.queueDepth));
    pos += appendChar(buf, pos, bufLen, ',');
    pos += appendU64(buf, pos, bufLen, static_cast<uint64_t>(snap.queueMaxDepth));
    pos += appendChar(buf, pos, bufLen, ',');
    pos += appendFloat(buf, pos, bufLen, snap.queueAvgDepth);
    pos += appendChar(buf, pos, bufLen, ',');
    pos += appendU64(buf, pos, bufLen, static_cast<uint64_t>(snap.peakHeapUsed));
    pos += appendChar(buf, pos, bufLen, ',');
    pos += appendU64(buf, pos, bufLen, static_cast<uint64_t>(snap.freeHeap));
    pos += appendChar(buf, pos, bufLen, ',');
    pos += appendU64(buf, pos, bufLen, static_cast<uint64_t>(snap.stackHighWater));
    pos += appendChar(buf, pos, bufLen, ',');
    pos += appendU64(buf, pos, bufLen, snap.totalGapUs);
    pos += appendChar(buf, pos, bufLen, ',');
    pos += appendU64(buf, pos, bufLen, snap.maxGapUs);
    pos += appendChar(buf, pos, bufLen, ',');
    pos += appendFloat(buf, pos, bufLen, snap.avgGapUs);
    pos += appendChar(buf, pos, bufLen, ',');
    pos += appendU32(buf, pos, bufLen, snap.schedulerTickCount);
    pos += appendChar(buf, pos, bufLen, ',');
    pos += appendU32(buf, pos, bufLen, snap.totalOverruns);
    pos += appendChar(buf, pos, bufLen, ',');
    pos += appendU32(buf, pos, bufLen, snap.totalDeadlineMisses);
    pos += appendChar(buf, pos, bufLen, '\n');

    // Task header
    pos += appendStr(buf, pos, bufLen, "task_idx,core,last,min,max,avg,samples,overruns,misses");
    for (std::size_t b = 0; b < Kernel::TaskTimer::kHistogramBuckets; ++b)
    {
        pos += appendStr(buf, pos, bufLen, ",h");
        pos += appendU32(buf, pos, bufLen, static_cast<uint32_t>(b));
    }
    pos += appendChar(buf, pos, bufLen, '\n');

    // Task rows
    for (std::size_t i = 0; i < snap.taskCount && i < kMaxSnapshotTasks; ++i)
    {
        const auto &t = snap.tasks[i];
        pos += appendU32(buf, pos, bufLen, static_cast<uint32_t>(t.taskIndex));
        pos += appendChar(buf, pos, bufLen, ',');
        pos += appendU32(buf, pos, bufLen, static_cast<uint32_t>(t.coreId));
        pos += appendChar(buf, pos, bufLen, ',');
        pos += appendU64(buf, pos, bufLen, t.lastUs);
        pos += appendChar(buf, pos, bufLen, ',');
        pos += appendU64(buf, pos, bufLen, t.minUs);
        pos += appendChar(buf, pos, bufLen, ',');
        pos += appendU64(buf, pos, bufLen, t.maxUs);
        pos += appendChar(buf, pos, bufLen, ',');
        pos += appendFloat(buf, pos, bufLen, t.avgUs);
        pos += appendChar(buf, pos, bufLen, ',');
        pos += appendU32(buf, pos, bufLen, t.samples);
        pos += appendChar(buf, pos, bufLen, ',');
        pos += appendU32(buf, pos, bufLen, t.overruns);
        pos += appendChar(buf, pos, bufLen, ',');
        pos += appendU32(buf, pos, bufLen, t.misses);
        for (std::size_t b = 0; b < Kernel::TaskTimer::kHistogramBuckets; ++b)
        {
            pos += appendChar(buf, pos, bufLen, ',');
            pos += appendU32(buf, pos, bufLen, t.histogram[b]);
        }
        pos += appendChar(buf, pos, bufLen, '\n');
    }

    // Null-terminate
    if (pos < bufLen)
        buf[pos] = '\0';
    else if (bufLen > 0)
        buf[bufLen - 1] = '\0';

    return pos;
}

} // namespace SputterOS
