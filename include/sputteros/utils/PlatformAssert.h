#ifndef SPUTTEROS_UTILS_PLATFORMASSERT_H
#define SPUTTEROS_UTILS_PLATFORMASSERT_H

/**
 * @file PlatformAssert.h
 * @brief Platform-independent assertion macro for SputterOS.
 *
 * Replaces `<cassert>` / `assert()` with a kernel-controlled assertion
 * mechanism that avoids pulling in the hosted C library header.
 *
 * - **Debug builds** (`NDEBUG` not defined): evaluates the expression and
 *   calls `SPUTTEROS_FAULT(file, line)` on failure, then enters an
 *   infinite loop to halt execution.
 * - **Release builds** (`NDEBUG` defined): expands to a no-op, identical
 *   to standard `assert()` behavior.
 *
 * ### Porting
 *
 * Override `SPUTTEROS_FAULT` before including this header to redirect
 * assertion failures to a platform-specific handler (e.g. breakpoint
 * instruction, UART dump, watchdog-triggered reset):
 *
 * @code
 * #define SPUTTEROS_FAULT(file, line) __BKPT(0)
 * #include "sputteros/utils/PlatformAssert.h"
 * @endcode
 *
 * @author Ryan Massie (rmassie)
 * @date 4/14/2026
 */

/**
 * @brief Default fault handler — enters an infinite spin loop.
 *
 * Override this macro before including PlatformAssert.h to provide
 * a platform-specific handler (breakpoint, LED blink, UART dump, etc.).
 *
 * @param file  Source file where the assertion failed (`__FILE__`).
 * @param line  Line number where the assertion failed (`__LINE__`).
 */
#ifndef SPUTTEROS_FAULT
#define SPUTTEROS_FAULT(file, line)                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        (void)(file);                                                                                                  \
        (void)(line);                                                                                                  \
        while (true)                                                                                                   \
        {                                                                                                              \
        }                                                                                                              \
    } while (0)
#endif

/**
 * @brief Kernel assertion macro — replaces `assert()` from `<cassert>`.
 *
 * In debug builds, evaluates @p expr and calls `SPUTTEROS_FAULT` on
 * failure.  In release builds (NDEBUG defined), compiles to nothing.
 *
 * @param expr  Boolean expression to evaluate.
 */
#ifdef NDEBUG
#define SPUTTEROS_ASSERT(expr) ((void)0)
#else
#define SPUTTEROS_ASSERT(expr)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr))                                                                                                   \
        {                                                                                                              \
            SPUTTEROS_FAULT(__FILE__, __LINE__);                                                                       \
        }                                                                                                              \
    } while (0)
#endif

#endif // SPUTTEROS_UTILS_PLATFORMASSERT_H
