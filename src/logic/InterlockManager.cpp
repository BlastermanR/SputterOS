/**
 * @file InterlockManager.cpp
 * @brief Implementation of the machine-agnostic safety interlock manager.
 *
 * Evaluates registered interlock conditions and provides soft abort/hard
 * fault handling via `IFaultResponse`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/6/2026
 */

#include "sputteros/logic/InterlockManager.h"

namespace SputterOS
{

InterlockManager::InterlockManager()
    : m_conditions{}, m_conditionCount(0), m_faultResponse(nullptr), m_softAbort(false), m_hardFault(false)
{
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

OpResult InterlockManager::registerCondition(IInterlockCondition *condition)
{
    if (!condition)
    {
        return OpResult::NULL_ARG;
    }
    if (m_conditionCount >= kMaxConditions)
    {
        return OpResult::FULL;
    }
    m_conditions[m_conditionCount++] = condition;
    return OpResult::OK;
}

void InterlockManager::setFaultResponse(IFaultResponse *response) { m_faultResponse = response; }

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool InterlockManager::checkAllInterlocks() const
{
    for (std::size_t i = 0; i < m_conditionCount; ++i)
    {
        if (!m_conditions[i]->isSafe())
        {
            return false;
        }
    }
    return true;
}

void InterlockManager::triggerSoftAbort() { m_softAbort.store(true, std::memory_order_release); }

void InterlockManager::triggerHardFault()
{
    m_hardFault.store(true, std::memory_order_release);

    if (m_faultResponse)
    {
        m_faultResponse->execute();
    }
}

bool InterlockManager::hasSoftAbort() const { return m_softAbort.load(std::memory_order_acquire); }

bool InterlockManager::isHardFaulted() const { return m_hardFault.load(std::memory_order_acquire); }

bool InterlockManager::clearFault()
{
    if (!checkAllInterlocks())
    {
        return false;
    }

    m_softAbort.store(false, std::memory_order_release);
    m_hardFault.store(false, std::memory_order_release);
    return true;
}

bool InterlockManager::validateDependencies() const { return m_conditionCount > 0 && m_faultResponse != nullptr; }

} // namespace SputterOS
