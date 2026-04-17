"""Tests for SputterClient high-level operations.

Uses a loopback transport that simulates device responses,
validating the handshake lifecycle, command send/ack, and
metrics request flow.
"""

import struct
import threading
import time
from typing import Callable, Optional

import pytest

from sputterctl.cobs import encode as cobs_encode
from sputterctl.protocol import (
    HANDSHAKE_MAGIC,
    PROTOCOL_VERSION,
    DecodedFrame,
    FrameDecoder,
    MessageType,
    crc16,
    encode_frame,
)
from sputterctl.transport import Transport


# ── Fake device: accepts frames on write, replays canned responses ───


class LoopbackTransport(Transport):
    """In-memory transport that decodes incoming frames and
       responds with canned device replies."""

    def __init__(self):
        self._open = False
        self._inbox: bytearray = bytearray()  # data available for read()
        self._outbox: bytearray = bytearray()  # raw bytes written by client
        self._lock = threading.Lock()
        self._decoder = FrameDecoder(self._on_frame)
        self._seq = 0

    # -- Transport interface ------------------------------------------------
    def connect(self) -> None:
        self._open = True

    def disconnect(self) -> None:
        self._open = False

    def write(self, data: bytes) -> None:
        with self._lock:
            self._outbox.extend(data)
            self._decoder.feed(data)

    def read(self, max_bytes: int = 1024) -> bytes:
        with self._lock:
            if not self._inbox:
                return b""
            chunk = bytes(self._inbox[:max_bytes])
            del self._inbox[:max_bytes]
            return chunk

    def is_connected(self) -> bool:
        return self._open

    # -- Device emulation ---------------------------------------------------
    def _on_frame(self, frame: DecodedFrame):
        """Generate a device response for each incoming frame."""
        if frame.msg_type == MessageType.HANDSHAKE_REQ:
            resp_payload = struct.pack("<HI", PROTOCOL_VERSION, HANDSHAKE_MAGIC)
            self._enqueue_response(MessageType.HANDSHAKE_RESP, frame.seq_num, resp_payload)

        elif frame.msg_type == MessageType.COMMAND:
            cmd_id = frame.payload[0:1] if frame.payload else b"\x00"
            self._enqueue_response(MessageType.ACK, frame.seq_num, cmd_id)

        elif frame.msg_type == MessageType.METRICS_REQ:
            self._enqueue_response(MessageType.METRICS_RESP, frame.seq_num, b"\xAA\xBB")

        elif frame.msg_type == MessageType.EXIT_HANDSHAKE:
            self._enqueue_response(MessageType.ACK, frame.seq_num, b"\x04")

        elif frame.msg_type == MessageType.HEARTBEAT:
            self._enqueue_response(MessageType.HEARTBEAT, frame.seq_num, b"")

    def _enqueue_response(self, msg_type: MessageType, seq: int, payload: bytes):
        wire = encode_frame(msg_type, seq, payload)
        self._inbox.extend(wire)


# ── Fixtures ──────────────────────────────────────────────────────────

@pytest.fixture()
def loopback():
    return LoopbackTransport()


@pytest.fixture()
def client(loopback):
    from sputterctl.client import SputterClient
    return SputterClient(loopback)


# ── Tests ─────────────────────────────────────────────────────────────


class TestHandshake:
    def test_connect_disconnect(self, client, loopback):
        result = client.connect(timeout=3.0)
        assert result is True
        assert client.is_connected
        client.disconnect()
        assert not client.is_connected

    def test_connect_sets_framed(self, client, loopback):
        client.connect(timeout=3.0)
        assert client.is_connected
        client.disconnect()


class TestCommand:
    def test_send_command_returns_ack(self, client, loopback):
        client.connect(timeout=3.0)

        resp = client.send_command(1, 0, 50.0, timeout=2.0)
        assert resp is not None
        assert resp.msg_type == MessageType.ACK

        client.disconnect()


class TestMetrics:
    def test_request_metrics(self, client, loopback):
        client.connect(timeout=3.0)

        data = client.request_metrics(timeout=2.0)
        assert data == b"\xAA\xBB"

        client.disconnect()


class TestHeartbeat:
    def test_heartbeat_no_error(self, client, loopback):
        client.connect(timeout=3.0)

        # send_heartbeat should not raise
        client.send_heartbeat()

        client.disconnect()
