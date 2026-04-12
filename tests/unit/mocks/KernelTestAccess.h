#ifndef SPUTTEROS_UNIT_MOCKS_KERNELTESTACCESS_H
#define SPUTTEROS_UNIT_MOCKS_KERNELTESTACCESS_H

#include "sputteros/kernel/tasks/BackgroundDiagnosticsTask.h"
#include "sputteros/kernel/tasks/ScheduledCommsTask.h"
#include "sputteros/kernel/tasks/ScheduledControlTask.h"
#include "sputteros/kernel/System.h"

/**
 * @file KernelTestAccess.h
 * @brief Test-only factory for kernel tasks with private constructors.
 *
 * Each kernel task declares `friend struct KernelTestAccess;` in the
 * `SputterOS::Kernel` namespace. This struct provides static factory
 * methods that unit tests can use to construct kernel tasks with mock
 * dependencies — bypassing the SystemBuilder which is the only
 * production-code path to create these tasks.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */

namespace SputterOS
{
namespace Kernel
{

struct KernelTestAccess
{
    /**
     * @brief Construct a ScheduledCommsTask with the given dependencies.
     */
    template <typename Cfg>
    static ScheduledCommsTask<Cfg> makeCommsTask(IStream *stream, ICommandProducer<Cfg> *commandQueue)
    {
        return ScheduledCommsTask<Cfg>(KernelConstructTag{}, stream, commandQueue);
    }

    /**
     * @brief Construct a ScheduledControlTask with the given dependencies.
     */
    template <typename Cfg>
    static ScheduledControlTask<Cfg> makeControlTask(ICommandConsumer<Cfg> *commandQueue, IUserApplication<Cfg> *app,
                                                     ISafetyMonitor **monitors, std::size_t monitorCount)
    {
        return ScheduledControlTask<Cfg>(KernelConstructTag{}, commandQueue, app, monitors, monitorCount);
    }

    /**
     * @brief Construct a BackgroundDiagnosticsTask with the given dependencies.
     */
    static BackgroundDiagnosticsTask makeDiagnosticsTask(ErrorLogger &logger, MemoryProfiler &memProfiler,
                                                         BackgroundDiagnosticsTask::WatchdogKickFn watchdogKick)
    {
        return BackgroundDiagnosticsTask(KernelConstructTag{}, logger, memProfiler, watchdogKick);
    }

    /**
     * @brief Reset the System singleton for test re-use between fixtures.
     */
    template <typename Cfg> static void resetSystem() { System<Cfg>::reset(); }

    /**
     * @brief Reset only the background task storage.
     */
    template <typename Cfg> static void resetBackgroundTasks()
    {
        System<Cfg>::s_backgroundTaskCount = 0;
        for (auto &t : System<Cfg>::s_backgroundTasks)
            t = nullptr;
    }

    /**
     * @brief Reset only the kernel state to UNCONFIGURED for test isolation.
     */
    template <typename Cfg> static void resetKernelState()
    {
        System<Cfg>::s_kernelState = Kernel::KernelState::UNCONFIGURED;
    }

    /**
     * @brief Proxy for System<Cfg>::transitionTo() — test access to private method.
     */
    template <typename Cfg> static bool transitionTo(Kernel::KernelState target)
    {
        return System<Cfg>::transitionTo(target);
    }
};

} // namespace Kernel
} // namespace SputterOS

#endif // SPUTTEROS_UNIT_MOCKS_KERNELTESTACCESS_H
