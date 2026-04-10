#ifndef SPUTTEROS_UTILS_MEMORYPROFILER_H
#define SPUTTEROS_UTILS_MEMORYPROFILER_H

#include <cstddef>

namespace SputterOS
{

/**
 * @file MemoryProfiler.h
 * @brief Heap and stack usage tracker for embedded memory safety.
 *
 * Tracks heap fragmentation and stack high-water marks to predict
 * out-of-memory faults before they manifest as hard faults or silent
 * data corruption.
 *
 * @note Static methods (`getFreeHeapBytes()`) rely on platform-specific
 *       hooks (e.g. `mallinfo()` on newlib). In unit tests these must
 *       be stubbed or the implementation skipped via `#ifndef NDEBUG`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/5/2026
 */
class MemoryProfiler
{
  public:
    /**
     * @brief Construct a MemoryProfiler and record the initial heap baseline.
     */
    MemoryProfiler();

    /**
     * @brief Sample current heap and stack usage and update peak records.
     *
     * Call periodically from `DiagnosticsTask::tick()` to keep statistics
     * up to date.
     */
    void update();

    /**
     * @brief Return the current number of free heap bytes.
     * @return Approximate free heap in bytes, or 0 if unsupported.
     */
    static std::size_t getFreeHeapBytes();

    /**
     * @brief Return the approximate stack high-water mark.
     * @return Bytes used at peak stack depth since construction, or 0 if unsupported.
     */
    static std::size_t getStackHighWaterMark();

    /**
     * @brief Return the peak heap bytes consumed since construction or last reset.
     * @return Peak heap usage in bytes.
     */
    std::size_t getPeakHeapUsedBytes() const;

    /**
     * @brief Reset peak-usage statistics without affecting hardware state.
     */
    void reset();

  private:
    std::size_t m_baselineFreeBytes; /**< @brief Free heap at construction time (bytes). */
    std::size_t m_peakUsedBytes;     /**< @brief Highest heap usage observed (bytes). */
};

}; // namespace SputterOS
#endif // SPUTTEROS_UTILS_MEMORYPROFILER_H
