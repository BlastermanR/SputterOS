#ifndef SPUTTEROS_UTILS_NONBLOCKINGSTOPWATCH_H
#define SPUTTEROS_UTILS_NONBLOCKINGSTOPWATCH_H

#include "sputteros/osal/SputterTime.h"
#include <cstdint>

namespace SputterOS
{

/**
 * @file NonBlockingStopwatch.h
 * @brief Elapsed-time tracker that never blocks.
 *
 * Records a start timestamp and computes elapsed time by comparing against
 * a caller-supplied current timestamp. Used by `StateMachine` to time state
 * phase durations without blocking delays.
 *
 * @note The caller is responsible for providing monotonically increasing
 *       timestamps.
 * @note All time parameters use `SputterMicros` (uint64_t microseconds).
 *
 * @author Ryan Massie (rmassie)
 * @date 4/7/2026
 */
class NonBlockingStopwatch
{
  public:
    /**
     * @brief Construct a NonBlockingStopwatch in the stopped/reset state.
     */
    NonBlockingStopwatch();

    /**
     * @brief Start or restart the stopwatch, recording the current time.
     * @param currentTime: Current monotonic time.
     */
    void start(SputterMicros currentTime);

    /**
     * @brief Stop the stopwatch without clearing the recorded start time.
     */
    void stop();

    /**
     * @brief Return time elapsed since the last `start()` call.
     * @param currentTime: Current monotonic time.
     * @return Elapsed time, or 0 ms if the stopwatch is stopped.
     */
    SputterMicros elapsed(SputterMicros currentTime) const;

    /**
     * @brief Check whether the elapsed time has met or exceeded a threshold.
     * @param currentTime: Current monotonic time.
     * @param duration: Expiry threshold.
     * @return true if elapsed time >= duration and the watch is running.
     */
    bool hasExpired(SputterMicros currentTime, SputterMicros duration) const;

    /**
     * @brief Check whether the stopwatch is currently running.
     * @return true if `start()` has been called more recently than `stop()`.
     */
    bool isRunning() const;

    /**
     * @brief Stop the stopwatch and clear the recorded start time.
     */
    void reset();

  private:
    SputterMicros m_startTime; /**< @brief Timestamp recorded by the last `start()`. */
    bool          m_running;   /**< @brief true while the watch is timing. */
};

}; // namespace SputterOS
#endif // SPUTTEROS_UTILS_NONBLOCKINGSTOPWATCH_H
