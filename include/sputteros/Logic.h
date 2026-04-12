#ifndef SPUTTEROS_LOGIC_H
#define SPUTTEROS_LOGIC_H

/**
 * @file Logic.h
 * @brief Logic component header — command parsing, interlocks, safety.
 *
 * Includes the command parser for serial protocol handling and the
 * interlock management system for machine safety.
 *
 * @code
 * #include "sputteros/Logic.h"
 * @endcode
 *
 * @author SputterOS Contributors
 * @date 4/11/2026
 */

// ── Command Parsing ─────────────────────────────────────────────────────────
#include "sputteros/logic/CommandParser.h"

// ── Interlock Safety ────────────────────────────────────────────────────────
#include "sputteros/logic/IFaultResponse.h"
#include "sputteros/logic/IInterlockCondition.h"
#include "sputteros/logic/InterlockManager.h"

#endif // SPUTTEROS_LOGIC_H
