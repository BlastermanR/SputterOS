#ifndef CRUNCHLOOP_INCLUDE_CRUNCHLOOPAPP_H
#define CRUNCHLOOP_INCLUDE_CRUNCHLOOPAPP_H

/**
 * @file CrunchLoopApp.h
 * @brief Trivial IUserApplication that writes servo targets to a double buffer.
 *
 * Each `tick()`, the application increments a target angle and publishes
 * it via an `AtomicDoubleBuffer`.  The crunch task on Core 1 reads the
 * latest target each iteration to simulate a servo control loop.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/14/2026
 */

#include "CrunchLoopConfig.h"
#include "sputteros/kernel/interfaces/IUserApplication.h"
#include "sputteros/osal/sync/AtomicDoubleBuffer.h"

#include <cstdint>
#include <cstdio>

namespace CrunchLoop
{

/**
 * @brief Servo target published from the application to the crunch task.
 */
struct TargetState
{
    float    angle{0.0f}; /**< @brief Desired servo angle in degrees. */
    uint32_t seqNum{0};   /**< @brief Monotonic sequence number. */
};

/**
 * @brief IUserApplication that publishes incrementing servo targets.
 */
class CrunchLoopApp : public SputterOS::IUserApplication<CrunchLoopConfig>
{
  public:
    /**
     * @brief Construct with a reference to the shared target buffer.
     * @param targetBuf Double buffer shared with `SimulatedServoTask`.
     */
    explicit CrunchLoopApp(SputterOS::AtomicDoubleBuffer<TargetState> &targetBuf) : m_targetBuf(targetBuf) {}

    void init() override {}

    void tick(SputterOS::SputterMicros /*systemTimeMicros*/) override
    {
        ++m_seq;
        m_angle += 0.5f;
        if (m_angle > 180.0f)
            m_angle = 0.0f;

        m_targetBuf.write({m_angle, m_seq});
    }

    void handleCommand(const CrunchLoopConfig::Command & /*cmd*/) override {}
    void forceSafeAbort() override {}

    /** @brief Number of targets published so far. */
    uint32_t seqNum() const { return m_seq; }

  private:
    SputterOS::AtomicDoubleBuffer<TargetState> &m_targetBuf;
    float                                       m_angle{0.0f};
    uint32_t                                    m_seq{0};
};

} // namespace CrunchLoop

#endif // CRUNCHLOOP_INCLUDE_CRUNCHLOOPAPP_H
