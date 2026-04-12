#ifndef SPUTTEROS_KERNEL_H
#define SPUTTEROS_KERNEL_H

/**
 * @file Kernel.h
 * @brief Kernel component header — System singleton, state machine, and tasks.
 *
 * Includes the runtime kernel infrastructure: the `System<Cfg>` singleton,
 * kernel lifecycle states, and the three built-in kernel tasks
 * (control, comms, diagnostics).
 *
 * @code
 * #include "sputteros/Kernel.h"
 * @endcode
 *
 * @author SputterOS Contributors
 * @date 4/11/2026
 */

// ── Kernel Core ─────────────────────────────────────────────────────────────
#include "sputteros/kernel/KernelConstructTag.h"
#include "sputteros/kernel/KernelState.h"
#include "sputteros/kernel/System.h"
#include "sputteros/kernel/TaskState.h"

// ── Kernel Tasks ────────────────────────────────────────────────────────────
#include "sputteros/kernel/BackgroundDiagnosticsTask.h"
#include "sputteros/kernel/ScheduledCommsTask.h"
#include "sputteros/kernel/ScheduledControlTask.h"

// ── Kernel Metrics ──────────────────────────────────────────────────────────
#include "sputteros/kernel/DeadlineTracker.h"
#include "sputteros/kernel/TaskTimer.h"

// ── Kernel Interfaces ───────────────────────────────────────────────────────
#include "sputteros/kernel/interfaces/ISafetyMonitor.h"
#include "sputteros/kernel/interfaces/IUserApplication.h"

#endif // SPUTTEROS_KERNEL_H
