#ifndef SPUTTEROS_UTILS_PERFORMANCEFORMATTER_H
#define SPUTTEROS_UTILS_PERFORMANCEFORMATTER_H

/**
 * @file PerformanceFormatter.h
 * @brief Heap-free formatter for PerformanceSnapshot data.
 *
 * Converts a `PerformanceSnapshot` into machine-parseable text
 * (key=value or CSV format) written into a caller-provided buffer.
 * Uses no heap allocation — all formatting is done with integer
 * arithmetic and fixed-point decimal rendering.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "sputteros/utils/PerformanceSnapshot.h"
#include <cstddef>
#include <cstdint>

namespace SputterOS
{

class PerformanceFormatter
{
  public:
    /**
     * @brief Format a snapshot as key=value pairs.
     *
     * Output format (one key per line, newline-terminated):
     * @code
     *   timestamp=123456789
     *   cores=2
     *   core0_util=0.25
     *   core1_util=0.10
     *   queue_depth=3
     *   queue_max=7
     *   queue_avg=2.500
     *   heap_peak=1024
     *   heap_free=0
     *   stack_hw=0
     *   gap_total=50000
     *   gap_max=200
     *   gap_avg=50.000
     *   sched_ticks=1000
     *   overruns=0
     *   deadline_misses=0
     *   task_count=2
     *   t0_core=0 t0_last=100 t0_min=80 t0_max=150 t0_avg=110.000 ...
     * @endcode
     *
     * @param snap:   Snapshot to format.
     * @param buf:    Destination buffer.
     * @param bufLen: Size of the destination buffer in bytes.
     * @return Number of bytes written (excluding null terminator),
     *         or 0 if the buffer is too small for even the header.
     */
    static std::size_t formatKeyValue(const PerformanceSnapshot &snap, char *buf, std::size_t bufLen);

    /**
     * @brief Format a snapshot as CSV (header row + data rows).
     *
     * Emits a system-level summary row followed by one row per task.
     *
     * @param snap:   Snapshot to format.
     * @param buf:    Destination buffer.
     * @param bufLen: Size of the destination buffer in bytes.
     * @return Number of bytes written (excluding null terminator).
     */
    static std::size_t formatCSV(const PerformanceSnapshot &snap, char *buf, std::size_t bufLen);

  private:
    /**
     * @brief Append a string to the buffer, respecting capacity.
     * @return Number of characters appended.
     */
    static std::size_t appendStr(char *buf, std::size_t pos, std::size_t bufLen, const char *str);

    /**
     * @brief Append a uint64 as decimal to the buffer.
     * @return Number of characters appended.
     */
    static std::size_t appendU64(char *buf, std::size_t pos, std::size_t bufLen, uint64_t val);

    /**
     * @brief Append a uint32 as decimal to the buffer.
     * @return Number of characters appended.
     */
    static std::size_t appendU32(char *buf, std::size_t pos, std::size_t bufLen, uint32_t val);

    /**
     * @brief Append a float with 3 decimal places to the buffer.
     * @return Number of characters appended.
     */
    static std::size_t appendFloat(char *buf, std::size_t pos, std::size_t bufLen, float val);

    /**
     * @brief Append a single character to the buffer.
     * @return 1 if appended, 0 if buffer full.
     */
    static std::size_t appendChar(char *buf, std::size_t pos, std::size_t bufLen, char c);
};

} // namespace SputterOS

#endif // SPUTTEROS_UTILS_PERFORMANCEFORMATTER_H
