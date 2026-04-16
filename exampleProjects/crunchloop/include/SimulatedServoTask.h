#ifndef CRUNCHLOOP_INCLUDE_SIMULATEDSERVOTASK_H
#define CRUNCHLOOP_INCLUDE_SIMULATEDSERVOTASK_H

/**
 * @file SimulatedServoTask.h
 * @brief ICrunchTask that simulates a blocking servo control loop.
 *
 * Each `crunch()` iteration reads the latest target angle from an
 * `AtomicDoubleBuffer`, simulates a short blocking SPI transaction
 * (~30 µs × 3 joints via a volatile busy loop), and writes encoder
 * feedback state back through a second double buffer.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/14/2026
 */

#include "CrunchLoopApp.h"
#include "sputteros/osal/SputterTime.h"
#include "sputteros/osal/sync/AtomicDoubleBuffer.h"
#include "sputteros/osal/tasks/ICrunchTask.h"

#include <cstdint>

namespace CrunchLoop
{

/**
 * @brief Encoder state published from the crunch task back to the application.
 */
struct EncoderState
{
    uint32_t                 iterationCount{0}; /**< @brief Total crunch iterations. */
    SputterOS::SputterMicros lastTimestamp{0};  /**< @brief Timestamp of last iteration. */
};

/**
 * @brief ICrunchTask simulating a multi-joint servo SPI control loop.
 *
 * Registered on Core 1 in CRUNCH dispatch mode.  Runs at ~6 kHz
 * (`crunchPeriodUs() = 163 µs`) with a declared WCET of 200 µs.
 */
class SimulatedServoTask : public SputterOS::ICrunchTask
{
  public:
    /**
     * @brief Construct with references to the shared double buffers.
     * @param targetBuf Buffer to read servo targets from.
     * @param stateBuf  Buffer to write encoder state to.
     */
    SimulatedServoTask(SputterOS::AtomicDoubleBuffer<TargetState>  &targetBuf,
                       SputterOS::AtomicDoubleBuffer<EncoderState> &stateBuf)
        : m_targetBuf(targetBuf), m_stateBuf(stateBuf)
    {
    }

    // -----------------------------------------------------------------
    // ITask interface (unused in CRUNCH mode, but required)
    // -----------------------------------------------------------------

    void init() override {}
    void tick(SputterOS::SputterMicros /*now*/) override {}

    // -----------------------------------------------------------------
    // ICrunchTask interface
    // -----------------------------------------------------------------

    SputterOS::SputterMicros crunchPeriodUs() const override { return 163; }
    SputterOS::SputterMicros maxIterationUs() const override { return 200; }

    void crunch(SputterOS::SputterMicros now) override
    {
        // Read latest target from the application (wait-free).
        auto target = m_targetBuf.read();
        (void)target; // Target consumed — would drive actuator in real code.

        ++m_iterationCount;

        // Simulate ~30 µs of blocking SPI per joint × 3 joints.
        volatile uint32_t sink = 0;
        for (uint32_t i = 0; i < 900; ++i)
            sink += i;
        (void)sink;

        // Publish encoder feedback state back to the application (wait-free).
        m_stateBuf.write({m_iterationCount, now});
    }

    void onCrunchAbort() override { m_aborted = true; }

    /** @brief Total crunch iterations executed. */
    uint32_t iterationCount() const { return m_iterationCount; }

    /** @brief Whether the task was aborted by the safety system. */
    bool wasAborted() const { return m_aborted; }

  private:
    SputterOS::AtomicDoubleBuffer<TargetState>  &m_targetBuf;
    SputterOS::AtomicDoubleBuffer<EncoderState> &m_stateBuf;
    uint32_t                                     m_iterationCount{0};
    bool                                         m_aborted{false};
};

} // namespace CrunchLoop

#endif // CRUNCHLOOP_INCLUDE_SIMULATEDSERVOTASK_H
