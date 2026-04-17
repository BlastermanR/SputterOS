"""SputterOS framed protocol implementation.

Mirrors the C++ protocol headers:
- MessageType enum
- Frame constants (header/trailer sizes, handshake magic)
- CRC16-CCITT
- Frame encoder/decoder
- Payload serializers/deserializers
"""

from __future__ import annotations

import struct
from enum import IntEnum
from typing import Callable, NamedTuple, Optional

from . import cobs

# =========================================================================
# Constants (mirror FrameConstants.h)
# =========================================================================

FRAME_HEADER_SIZE = 4  # MsgType(1) + SeqNum(1) + PayloadLen(2LE)
FRAME_TRAILER_SIZE = 2  # CRC16(2)
FRAME_OVERHEAD = FRAME_HEADER_SIZE + FRAME_TRAILER_SIZE
FRAME_DELIMITER = 0x00
PROTOCOL_VERSION = 1
HANDSHAKE_MAGIC = 0x53504F53  # "SPOS" little-endian


# =========================================================================
# MessageType (mirror MessageType.h)
# =========================================================================


class MessageType(IntEnum):
    """Protocol message type identifiers."""

    # Host → Device
    COMMAND = 0x01
    HANDSHAKE_REQ = 0x02
    METRICS_REQ = 0x03
    EXIT_HANDSHAKE = 0x04

    # Device → Host
    ACK = 0x81
    NACK = 0x82
    HANDSHAKE_RESP = 0x83
    TELEMETRY = 0x84
    METRICS_RESP = 0x85
    LOG = 0x86
    DATA = 0x87
    PERF_DATA = 0x88

    # Bidirectional
    HEARTBEAT = 0xF0


# =========================================================================
# CRC16-CCITT
# =========================================================================


def crc16(data: bytes, init: int = 0xFFFF) -> int:
    """Compute CRC16-CCITT (polynomial 0x1021)."""
    crc = init
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


# =========================================================================
# Frame Encoder
# =========================================================================


def encode_frame(msg_type: MessageType, seq_num: int, payload: bytes = b"") -> bytes:
    """Encode a message into a COBS wire frame.

    Returns bytes ready for transmission: ``COBS(header + payload + CRC) + 0x00``.
    """
    # Build raw frame: header + payload + CRC
    header = struct.pack("<BBH", int(msg_type), seq_num, len(payload))
    raw = header + payload
    checksum = crc16(raw)
    raw += struct.pack("<H", checksum)

    # COBS encode + delimiter
    encoded = cobs.encode(raw)
    return encoded + bytes([FRAME_DELIMITER])


# =========================================================================
# Frame Decoder
# =========================================================================


class DecodedFrame(NamedTuple):
    """Result of successfully decoding a frame."""

    msg_type: MessageType
    seq_num: int
    payload: bytes


class FrameDecoder:
    """Stateful byte-by-byte frame accumulator and decoder.

    Feed bytes via :meth:`feed_byte`. When a complete frame is decoded,
    the provided callback is invoked with a :class:`DecodedFrame`.
    """

    def __init__(self, on_frame: Callable[[DecodedFrame], None]) -> None:
        self._on_frame = on_frame
        self._buf = bytearray()

    def feed(self, data: bytes) -> None:
        """Feed a chunk of bytes into the decoder."""
        for b in data:
            self.feed_byte(b)

    def feed_byte(self, byte: int) -> None:
        """Feed a single byte into the decoder."""
        if byte == FRAME_DELIMITER:
            if len(self._buf) > 0:
                self._try_decode()
            self._buf.clear()
        else:
            self._buf.append(byte)

    def reset(self) -> None:
        """Discard any partially accumulated frame data."""
        self._buf.clear()

    def _try_decode(self) -> None:
        """Attempt to COBS-decode and validate the accumulated buffer."""
        try:
            raw = cobs.decode(bytes(self._buf))
        except ValueError:
            return  # Malformed COBS — discard.

        if len(raw) < FRAME_OVERHEAD:
            return  # Too short for header + CRC.

        # Validate CRC (covers header + payload).
        frame_data = raw[:-FRAME_TRAILER_SIZE]
        stored_crc = struct.unpack_from("<H", raw, len(raw) - 2)[0]
        if crc16(frame_data) != stored_crc:
            return  # CRC mismatch.

        # Parse header.
        msg_type_raw, seq_num, payload_len = struct.unpack_from("<BBH", frame_data)
        if FRAME_HEADER_SIZE + payload_len != len(frame_data):
            return  # Payload length mismatch.

        try:
            msg_type = MessageType(msg_type_raw)
        except ValueError:
            return  # Unknown message type.

        payload = frame_data[FRAME_HEADER_SIZE:]
        self._on_frame(DecodedFrame(msg_type, seq_num, payload))


# =========================================================================
# Payload serializers (Host → Device)
# =========================================================================


def encode_command(cmd_id: int, device: int, value: float) -> bytes:
    """Serialize a COMMAND payload: [CmdID:1][device:1][value:4LE]."""
    return struct.pack("<BBf", cmd_id, device, value)


def encode_handshake_req() -> bytes:
    """Serialize a HANDSHAKE_REQ payload: [version:2LE][magic:4LE]."""
    return struct.pack("<HI", PROTOCOL_VERSION, HANDSHAKE_MAGIC)


# =========================================================================
# Payload deserializers (Device → Host)
# =========================================================================


def decode_ack(payload: bytes) -> Optional[int]:
    """Decode an ACK payload → cmd_id, or None on error."""
    if len(payload) != 1:
        return None
    return payload[0]


def decode_nack(payload: bytes) -> Optional[tuple[int, int, float]]:
    """Decode a NACK payload → (cmd_id, device, value), or None."""
    if len(payload) != 6:
        return None
    cmd_id, device, value = struct.unpack_from("<BBf", payload)
    return (cmd_id, device, value)


def decode_handshake_resp(payload: bytes) -> Optional[tuple[int, int]]:
    """Decode HANDSHAKE_RESP → (version, magic), or None."""
    if len(payload) != 6:
        return None
    version, magic = struct.unpack_from("<HI", payload)
    return (version, magic)


def decode_log(payload: bytes) -> Optional[tuple[int, str]]:
    """Decode LOG payload → (level, text), or None."""
    if len(payload) < 1:
        return None
    level = payload[0]
    text = payload[1:].decode("utf-8", errors="replace")
    return (level, text)


# =========================================================================
# Metrics deserialization (Device → Host)
# =========================================================================

# Wire sizes must match C++ ResponseSerializer constants.
_METRICS_HEADER_SIZE = 66
_METRICS_TASK_SIZE = 26


class TaskMetrics(NamedTuple):
    """Per-task timing metrics."""

    task_index: int
    core_id: int
    last_us: int
    min_us: int
    max_us: int
    avg_us: float
    samples: int
    overruns: int
    misses: int


class PerformanceMetrics(NamedTuple):
    """Decoded PerformanceSnapshot from a METRICS_RESP payload."""

    timestamp: int
    core_count: int
    core_utilization: tuple[float, float, float, float]
    queue_depth: int
    queue_max_depth: int
    queue_avg_depth: float
    peak_heap_used: int
    free_heap: int
    stack_high_water: int
    total_gap_us: int
    max_gap_us: int
    avg_gap_us: float
    scheduler_tick_count: int
    total_overruns: int
    total_deadline_misses: int
    tasks: list[TaskMetrics]


def decode_metrics(payload: bytes) -> Optional[PerformanceMetrics]:
    """Decode a METRICS_RESP payload into a PerformanceMetrics object.

    Returns None if the payload is too short or structurally invalid.
    """
    if len(payload) < _METRICS_HEADER_SIZE:
        return None

    off = 0
    (timestamp,) = struct.unpack_from("<Q", payload, off); off += 8
    (core_count,) = struct.unpack_from("<B", payload, off); off += 1
    core_util = struct.unpack_from("<4f", payload, off); off += 16
    queue_depth, queue_max_depth = struct.unpack_from("<HH", payload, off); off += 4
    (queue_avg_depth,) = struct.unpack_from("<f", payload, off); off += 4
    peak_heap, free_heap, stack_hw = struct.unpack_from("<III", payload, off); off += 12
    total_gap, max_gap = struct.unpack_from("<II", payload, off); off += 8
    (avg_gap,) = struct.unpack_from("<f", payload, off); off += 4
    (tick_count,) = struct.unpack_from("<I", payload, off); off += 4
    total_overruns, total_misses = struct.unpack_from("<HH", payload, off); off += 4
    (task_count,) = struct.unpack_from("<B", payload, off); off += 1

    expected = _METRICS_HEADER_SIZE + task_count * _METRICS_TASK_SIZE
    if len(payload) < expected:
        return None

    tasks: list[TaskMetrics] = []
    for _ in range(task_count):
        ti, ci = struct.unpack_from("<BB", payload, off); off += 2
        last, mn, mx = struct.unpack_from("<III", payload, off); off += 12
        (avg,) = struct.unpack_from("<f", payload, off); off += 4
        (samples,) = struct.unpack_from("<I", payload, off); off += 4
        ovr, mis = struct.unpack_from("<HH", payload, off); off += 4
        tasks.append(TaskMetrics(ti, ci, last, mn, mx, avg, samples, ovr, mis))

    return PerformanceMetrics(
        timestamp=timestamp,
        core_count=core_count,
        core_utilization=core_util,
        queue_depth=queue_depth,
        queue_max_depth=queue_max_depth,
        queue_avg_depth=queue_avg_depth,
        peak_heap_used=peak_heap,
        free_heap=free_heap,
        stack_high_water=stack_hw,
        total_gap_us=total_gap,
        max_gap_us=max_gap,
        avg_gap_us=avg_gap,
        scheduler_tick_count=tick_count,
        total_overruns=total_overruns,
        total_deadline_misses=total_misses,
        tasks=tasks,
    )
