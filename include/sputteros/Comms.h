#ifndef SPUTTEROS_COMMS_H
#define SPUTTEROS_COMMS_H

/**
 * @file Comms.h
 * @brief Communications component umbrella header.
 *
 * Provides the dual-mode CLI class, COBS framing codec, CRC16 integrity,
 * message types, frame encoder/decoder, protocol router, and response
 * serialization — everything needed for TEXT + FRAMED serial comms.
 *
 * @code
 * #include "sputteros/Comms.h"
 * @endcode
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "sputteros/comms/CLI.h"
#include "sputteros/comms/protocol/CobsCodec.h"
#include "sputteros/comms/protocol/CommsMode.h"
#include "sputteros/comms/protocol/Crc16.h"
#include "sputteros/comms/protocol/FrameConstants.h"
#include "sputteros/comms/protocol/FrameDecoder.h"
#include "sputteros/comms/protocol/FrameEncoder.h"
#include "sputteros/comms/protocol/IProtocolHandler.h"
#include "sputteros/comms/protocol/MessageType.h"
#include "sputteros/comms/protocol/ProtocolRouter.h"
#include "sputteros/comms/protocol/ResponseSerializer.h"

#endif // SPUTTEROS_COMMS_H
