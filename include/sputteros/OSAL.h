#ifndef SPUTTEROS_OSAL_H
#define SPUTTEROS_OSAL_H

/**
 * @file OSAL.h
 * @brief OS Abstraction Layer component header — tasks, sync, time.
 *
 * Includes the full task hierarchy (scheduled, background), inter-core
 * synchronization primitives, and the microsecond timing foundation.
 *
 * @code
 * #include "sputteros/OSAL.h"
 * @endcode
 *
 * @author SputterOS Contributors
 * @date 4/11/2026
 */

// ── Time ────────────────────────────────────────────────────────────────────
#include "sputteros/osal/SputterTime.h"

// ── Task Interfaces ─────────────────────────────────────────────────────────
#include "sputteros/osal/tasks/IBackgroundTask.h"
#include "sputteros/osal/tasks/IScheduledTask.h"
#include "sputteros/osal/tasks/ITask.h"

// ── Synchronization ─────────────────────────────────────────────────────────
#include "sputteros/osal/sync/ICommandConsumer.h"
#include "sputteros/osal/sync/ICommandProducer.h"
#include "sputteros/osal/sync/ICoreErrorHandler.h"
#include "sputteros/osal/sync/IMessageQueue.h"
#include "sputteros/osal/sync/LockFreeQueue.h"
#include "sputteros/osal/sync/MultiCoreSync.h"
#include "sputteros/osal/sync/WatchdogSync.h"

#endif // SPUTTEROS_OSAL_H
