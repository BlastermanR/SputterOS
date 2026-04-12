#ifndef SPUTTEROS_KERNEL_SCHEDULEDCONTROLTASK_H
#define SPUTTEROS_KERNEL_SCHEDULEDCONTROLTASK_H

#include "sputteros/ConfigTraits.h"
#include "sputteros/kernel/KernelConstructTag.h"
#include "sputteros/kernel/interfaces/ISafetyMonitor.h"
#include "sputteros/kernel/interfaces/IUserApplication.h"
#include "sputteros/osal/sync/ICommandConsumer.h"
#include "sputteros/osal/tasks/IScheduledTask.h"

/**
 * @file ScheduledControlTask.h
 * @brief Kernel control task - the deterministic execution core of SputterOS.
 *
 * Pops commands from the shared `ICommandConsumer`, evaluates safety
 * monitors, and ticks the user-supplied `IUserApplication` — all within
 * a strictly deterministic control loop.
 *
 * The kernel no longer directly references hardware registries, arc
 * detectors, or interlock managers. All domain-specific safety logic is
 * encapsulated behind the `ISafetyMonitor` interface, and all application
 * logic is encapsulated behind `IUserApplication`.
 *
 * @note This is a kernel task. Its constructor requires a `KernelConstructTag` —
 *       only `SystemBuilder` may instantiate it. The instance is owned by
 *       `System<Cfg>` as an `inline static` member.
 * @note Inherits `IScheduledTask` - pinned to Core 0 in multi-core configs.
 * @note Must run at a fixed, predictable rate (e.g. 100 Hz).
 *
 * @tparam Cfg Configuration struct providing `Command`, `kMaxCommandsPerTick`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */
namespace SputterOS
{

template <typename Cfg> class SystemBuilder;

namespace Kernel
{

struct KernelTestAccess;

template <typename Cfg> class ScheduledControlTask : public IScheduledTask
{
  public:
    using CommandStruct = typename Cfg::Command;

    /**
     * @brief The task's activation period in microseconds.
     * @return Period from `CfgControlBudgetUs<Cfg>`.
     */
    SputterMicros periodUs() const override { return CfgControlBudgetUs<Cfg>::value; }

    /**
     * @brief Initialize the user application.
     */
    void init() override
    {
        if (m_app)
        {
            m_app->init();
        }
    }

    /**
     * @brief Execute one control cycle.
     *
     * Sequence per tick:
     * 1. Evaluate all `ISafetyMonitor` instances; force abort if any fails.
     * 2. Drain pending commands from the queue via `processCommands()`.
     * 3. Tick the `IUserApplication` with the current monotonic timestamp.
     */
    void tick(SputterMicros systemTimeMicros) override
    {
        if (!evaluateSafety())
        {
            return;
        }

        processCommands(systemTimeMicros);

        if (m_app)
        {
            m_app->tick(systemTimeMicros);
        }
    }

    /**
     * @brief Validate that all required injected dependencies are non-null.
     * @return true if every pointer is valid, false if any dependency is null.
     */
    bool validateDependencies() const override { return m_commandQueue != nullptr && m_app != nullptr; }

    /**
     * @brief PassKey constructor — only SystemBuilder and KernelTestAccess may instantiate.
     * @param tag: Opaque access token (see KernelConstructTag.h).
     * @param commandQueue: Consumer-side queue from which commands are drained.
     * @param app: User application ticked each control cycle.
     * @param monitors: Array of safety monitors evaluated each tick.
     * @param monitorCount: Number of elements in the monitors array.
     */
    ScheduledControlTask(KernelConstructTag /*tag*/, ICommandConsumer<Cfg> *commandQueue, IUserApplication<Cfg> *app,
                         ISafetyMonitor **monitors, std::size_t monitorCount)
        : m_commandQueue(commandQueue), m_app(app), m_monitors(monitors), m_monitorCount(monitorCount)
    {
    }

  private:
    friend class SystemBuilder<Cfg>;
    friend struct KernelTestAccess;

    /**
     * --------------------
     * Injected Dependencies
     * --------------------
     */
    ICommandConsumer<Cfg> *m_commandQueue; /**< @brief Consumer-side command queue. */
    IUserApplication<Cfg> *m_app;          /**< @brief User application logic. */
    ISafetyMonitor       **m_monitors;     /**< @brief Array of safety monitors. */
    std::size_t            m_monitorCount; /**< @brief Number of safety monitors. */

    /**
     * --------------------
     * Private Helpers
     * --------------------
     */

    /**
     * @brief Drain all pending commands from the queue and forward them to
     *        the user application.
     */
    void processCommands(SputterMicros /*systemTimeMicros*/)
    {
        if (!m_commandQueue || !m_app)
        {
            return;
        }

        CommandStruct cmd{};
        int           processed = 0;

        while (processed < CfgMaxCommandsPerTick<Cfg>::value && m_commandQueue->try_pop(cmd))
        {
            m_app->handleCommand(cmd);
            ++processed;
        }
    }

    /**
     * @brief Evaluate all registered safety monitors.
     * @return true if every monitor reports safe, false if any failsafe fires.
     */
    bool evaluateSafety()
    {
        for (std::size_t i = 0; i < m_monitorCount; ++i)
        {
            if (m_monitors[i] && !m_monitors[i]->isSafe())
            {
                if (m_app)
                {
                    m_app->forceSafeAbort();
                }
                return false;
            }
        }
        return true;
    }
};

} // namespace Kernel
} // namespace SputterOS

#endif // SPUTTEROS_KERNEL_SCHEDULEDCONTROLTASK_H
