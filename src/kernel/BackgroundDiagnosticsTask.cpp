/**
 * @file BackgroundDiagnosticsTask.cpp
 * @brief Kernel diagnostics and health monitoring task implementation.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */

#include "sputteros/kernel/BackgroundDiagnosticsTask.h"
#include "sputteros/osal/tasks/ITask.h"
#include "sputteros/utils/logging/ErrorLogger.h"

namespace SputterOS
{
namespace Kernel
{

BackgroundDiagnosticsTask::BackgroundDiagnosticsTask(KernelConstructTag /*tag*/, ErrorLogger &logger, MemoryProfiler &memProfiler,
                                 WatchdogKickFn watchdogKick, uint32_t controlBudgetUs)
    : m_logger(logger), m_memProfiler(memProfiler), m_watchdogKick(watchdogKick), m_monitoredTasks{},
      m_monitoredCount(0), m_controlBudget(controlBudgetUs), m_tickCount(0)
{
    // Intentionally Empty
}

void BackgroundDiagnosticsTask::setMonitoredTasks(ITask *const *tasks, std::size_t count)
{
    m_monitoredCount = (count < kMaxMonitoredTasks) ? count : kMaxMonitoredTasks;
    for (std::size_t i = 0; i < m_monitoredCount; ++i)
    {
        m_monitoredTasks[i] = tasks[i];
    }
}

void BackgroundDiagnosticsTask::init()
{
    m_memProfiler.reset();
    for (std::size_t i = 0; i < m_monitoredCount; ++i)
    {
        if (m_monitoredTasks[i])
        {
            m_monitoredTasks[i]->timer().reset();
        }
    }
}

void BackgroundDiagnosticsTask::tick(SputterMicros /*systemTimeMicros*/)
{
    if (m_watchdogKick)
    {
        m_watchdogKick();
    }

    // Scan all monitored tasks for budget violations
    for (std::size_t i = 0; i < m_monitoredCount; ++i)
    {
        if (m_monitoredTasks[i] && m_monitoredTasks[i]->timer().isOverBudget(m_controlBudget))
        {
            m_logger.log(ErrorLogger::ErrorCode::SENSOR_ERROR, SputterMicros(0),
                         static_cast<float>(m_monitoredTasks[i]->timer().lastDuration()));
        }
    }

    m_memProfiler.update();

    ++m_tickCount;
    if (m_tickCount >= kMemCheckInterval)
    {
        m_tickCount = 0;
        m_logger.log(ErrorLogger::ErrorCode::WATCHDOG_KICK, SputterMicros(0),
                     static_cast<float>(m_memProfiler.getPeakHeapUsedBytes()));
    }
}

} // namespace Kernel
} // namespace SputterOS
