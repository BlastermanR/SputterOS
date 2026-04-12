"""Tests for the framed protocol implementation.

Validates CRC16, frame encoding/decoding roundtrips, payload
serializers, and FrameDecoder state machine.
"""

import struct

import pytest

from sputterctl.protocol import (
    HANDSHAKE_MAGIC,
    PROTOCOL_VERSION,
    DecodedFrame,
    FrameDecoder,
    MessageType,
    crc16,
    decode_ack,
    decode_handshake_resp,
    decode_log,
    decode_nack,
    encode_command,
    encode_frame,
    encode_handshake_req,
)


# ── CRC16 ────────────────────────────────────────────────────────────


class TestCrc16:
    def test_empty(self):
        assert crc16(b"") == 0xFFFF

    def test_known_vector(self):
        # "123456789" → CRC16-CCITT = 0x29B1
        assert crc16(b"123456789") == 0x29B1

    def test_single_byte(self):
        result = crc16(b"\x00")
        assert isinstance(result, int)
        assert 0 <= result <= 0xFFFF


# ── Frame encode/decode roundtrip ────────────────────────────────────


class TestFrameRoundtrip:
    def _roundtrip(self, msg_type: MessageType, seq: int, payload: bytes) -> DecodedFrame:
        """Encode a frame, decode it, and return the result."""
        wire = encode_frame(msg_type, seq, payload)
        frames: list[DecodedFrame] = []
        decoder = FrameDecoder(frames.append)
        decoder.feed(wire)
        assert len(frames) == 1, f"Expected 1 frame, got {len(frames)}"
        return frames[0]

    def test_ack(self):
        f = self._roundtrip(MessageType.ACK, 7, b"\x01")
        assert f.msg_type == MessageType.ACK
        assert f.seq_num == 7
        assert f.payload == b"\x01"

    def test_command(self):
        payload = encode_command(1, 0, 50.5)
        f = self._roundtrip(MessageType.COMMAND, 42, payload)
        assert f.msg_type == MessageType.COMMAND
        assert f.seq_num == 42
        assert f.payload == payload

    def test_handshake_req(self):
        payload = encode_handshake_req()
        f = self._roundtrip(MessageType.HANDSHAKE_REQ, 1, payload)
        assert f.msg_type == MessageType.HANDSHAKE_REQ
        assert f.payload == payload

    def test_empty_payload(self):
        f = self._roundtrip(MessageType.METRICS_REQ, 10, b"")
        assert f.msg_type == MessageType.METRICS_REQ
        assert f.payload == b""

    def test_heartbeat(self):
        f = self._roundtrip(MessageType.HEARTBEAT, 255, b"")
        assert f.msg_type == MessageType.HEARTBEAT
        assert f.seq_num == 255


# ── FrameDecoder robustness ──────────────────────────────────────────


class TestFrameDecoder:
    def test_garbage_before_frame(self):
        """Garbage bytes before a valid frame should be discarded."""
        frames: list[DecodedFrame] = []
        decoder = FrameDecoder(frames.append)

        garbage = b"\xDE\xAD\xBE\xEF"
        valid = encode_frame(MessageType.ACK, 1, b"\x01")

        decoder.feed(garbage + b"\x00" + valid)
        assert len(frames) == 1
        assert frames[0].msg_type == MessageType.ACK

    def test_corrupt_crc(self):
        """Corrupted frame should be silently dropped."""
        wire = bytearray(encode_frame(MessageType.ACK, 1, b"\x01"))
        # Corrupt a byte in the middle.
        if len(wire) > 3:
            wire[2] ^= 0xFF

        frames: list[DecodedFrame] = []
        decoder = FrameDecoder(frames.append)
        decoder.feed(bytes(wire))
        assert len(frames) == 0

    def test_multiple_frames(self):
        """Multiple concatenated frames should all be decoded."""
        frames: list[DecodedFrame] = []
        decoder = FrameDecoder(frames.append)

        f1 = encode_frame(MessageType.ACK, 1, b"\x01")
        f2 = encode_frame(MessageType.HEARTBEAT, 2, b"")
        f3 = encode_frame(MessageType.COMMAND, 3, encode_command(0, 0, 1.0))

        decoder.feed(f1 + f2 + f3)
        assert len(frames) == 3
        assert frames[0].msg_type == MessageType.ACK
        assert frames[1].msg_type == MessageType.HEARTBEAT
        assert frames[2].msg_type == MessageType.COMMAND

    def test_reset(self):
        """Reset should discard partial frame data."""
        frames: list[DecodedFrame] = []
        decoder = FrameDecoder(frames.append)

        wire = encode_frame(MessageType.ACK, 1, b"\x01")
        decoder.feed(wire[:3])  # Partial.
        decoder.reset()
        decoder.feed(wire)  # Full frame.
        assert len(frames) == 1


# ── Payload serializers ──────────────────────────────────────────────


class TestPayloads:
    def test_encode_command(self):
        payload = encode_command(1, 0, 50.0)
        assert len(payload) == 6
        cmd_id, device = payload[0], payload[1]
        value = struct.unpack_from("<f", payload, 2)[0]
        assert cmd_id == 1
        assert device == 0
        assert abs(value - 50.0) < 1e-6

    def test_encode_handshake_req(self):
        payload = encode_handshake_req()
        assert len(payload) == 6
        version, magic = struct.unpack_from("<HI", payload)
        assert version == PROTOCOL_VERSION
        assert magic == HANDSHAKE_MAGIC


# ── Payload deserializers ────────────────────────────────────────────


class TestDecoders:
    def test_decode_ack(self):
        assert decode_ack(b"\x03") == 3

    def test_decode_ack_wrong_len(self):
        assert decode_ack(b"\x01\x02") is None

    def test_decode_nack(self):
        payload = struct.pack("<BBf", 2, 5, 99.5)
        result = decode_nack(payload)
        assert result is not None
        assert result[0] == 2
        assert result[1] == 5
        assert abs(result[2] - 99.5) < 1e-6

    def test_decode_nack_wrong_len(self):
        assert decode_nack(b"\x01") is None

    def test_decode_handshake_resp(self):
        payload = struct.pack("<HI", PROTOCOL_VERSION, HANDSHAKE_MAGIC)
        result = decode_handshake_resp(payload)
        assert result is not None
        assert result[0] == PROTOCOL_VERSION
        assert result[1] == HANDSHAKE_MAGIC

    def test_decode_log(self):
        payload = bytes([2]) + b"hello"
        result = decode_log(payload)
        assert result is not None
        assert result[0] == 2
        assert result[1] == "hello"

    def test_decode_log_empty_text(self):
        result = decode_log(bytes([1]))
        assert result is not None
        assert result[0] == 1
        assert result[1] == ""

    def test_decode_log_empty_payload(self):
        assert decode_log(b"") is None
