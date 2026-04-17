#ifndef PINGPONG_OSAL_IMPL_PINGPONGOSAL_H
#define PINGPONG_OSAL_IMPL_PINGPONGOSAL_H

/**
 * @file PingPongOSAL.h
 * @brief OS-native platform utilities for the PingPong dual-core example.
 *
 * Re-exports the shared host time source under the PingPong namespace.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/10/2026
 */

#include "common/osal/HostTimeMicros.h"

namespace PingPong
{
using ExamplesCommon::platformGetTimeMicros;
} // namespace PingPong

#endif // PINGPONG_OSAL_IMPL_PINGPONGOSAL_H
