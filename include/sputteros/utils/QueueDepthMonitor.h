#ifndef SPUTTEROS_UTILS_QUEUEDEPTHMONITOR_H
#define SPUTTEROS_UTILS_QUEUEDEPTHMONITOR_H

/**
 * @file QueueDepthMonitor.h
 * @brief Tracks command queue occupancy over time.
 *
 * Periodically sampled by `BackgroundDiagnosticsTask` to record the
 * current queue depth. Maintains last, max, and running average depth
 * for performance analysis.
 *
 * @note Zero-heap, header-only. No dynamic allocation.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include <cstddef>
#include <cstdint>
#include <limits>

namespace SputterOS
{

class QueueDepthMonitor
{
  public:
    /**
     * @brief Record a queue depth sample.
     * @param currentDepth: Current number of elements in the queue.
     */
    void sample(std::size_t currentDepth)
    {
        m_lastDepth = currentDepth;
        if (currentDepth > m_maxDepth)
        {
            m_maxDepth = currentDepth;
        }
        if (m_sampleCount < std::numeric_limits<uint32_t>::max())
        {
            m_sumDepth += currentDepth;
            ++m_sampleCount;
        }
    }

    /**
     * @brief Most recent sampled depth.
     */
    std::size_t lastDepth() const { return m_lastDepth; }

    /**
     * @brief Peak depth observed since construction or last reset.
     */
    std::size_t maxDepth() const { return m_maxDepth; }

    /**
     * @brief Rolling average depth across all samples.
     * @return Average depth, or 0.0f if no samples recorded.
     */
    float averageDepth() const
    {
        if (m_sampleCount == 0)
        {
            return 0.0f;
        }
        return static_cast<float>(m_sumDepth) / static_cast<float>(m_sampleCount);
    }

    /**
     * @brief Number of depth samples taken.
     */
    uint32_t sampleCount() const { return m_sampleCount; }

    /**
     * @brief Reset all tracked state.
     */
    void reset()
    {
        m_lastDepth   = 0;
        m_maxDepth    = 0;
        m_sumDepth    = 0;
        m_sampleCount = 0;
    }

  private:
    std::size_t m_lastDepth{0};   /**< @brief Most recent depth sample. */
    std::size_t m_maxDepth{0};    /**< @brief Peak depth since reset. */
    uint64_t    m_sumDepth{0};    /**< @brief Sum of all depth samples for averaging. */
    uint32_t    m_sampleCount{0}; /**< @brief Number of samples taken. */
};

} // namespace SputterOS

#endif // SPUTTEROS_UTILS_QUEUEDEPTHMONITOR_H
