"""High-level SputterOS client.

Manages the connection lifecycle (handshake → commands → exit),
sequence number tracking, and async message dispatch.
"""

from __future__ import annotations

import threading
import time
from typing import Callable, Optional

from .protocol import (
    HANDSHAKE_MAGIC,
    DecodedFrame,
    FrameDecoder,
    MessageType,
    decode_ack,
    decode_handshake_resp,
    decode_log,
    decode_nack,
    encode_command,
    encode_frame,
    encode_handshake_req,
)
from .transport import Transport


class SputterClient:
    """Thread-safe client for SputterOS framed protocol.

    Usage::

        client = SputterClient(transport)
        client.connect()          # Handshake negotiation
        ack = client.send_command(1, 0, 50.0)
        metrics = client.request_metrics()
        client.disconnect()       # Exit handshake
    """

    def __init__(self, transport: Transport) -> None:
        self._transport = transport
        self._seq: int = 0
        self._connected: bool = False
        self._decoder = FrameDecoder(self._on_frame)
        self._lock = threading.Lock()

        # Pending response tracking
        self._pending_ack: Optional[DecodedFrame] = None
        self._pending_metrics: Optional[DecodedFrame] = None
        self._pending_handshake: Optional[DecodedFrame] = None
        self._event = threading.Event()

        # User callbacks
        self._on_telemetry: Optional[Callable[[bytes], None]] = None
        self._on_data: Optional[Callable[[bytes], None]] = None
        self._on_log: Optional[Callable[[int, str], None]] = None

        # Background reader thread
        self._reader_thread: Optional[threading.Thread] = None
        self._running: bool = False

    # ─── Lifecycle ────────────────────────────────────────────────────

    def connect(self, timeout: float = 3.0) -> bool:
        """Open transport and negotiate FRAMED mode handshake.

        Returns True if handshake succeeded, False on timeout.
        """
        self._transport.connect()
        self._running = True
        self._reader_thread = threading.Thread(target=self._reader_loop, daemon=True)
        self._reader_thread.start()

        # Send handshake: leading 0x00 sync + HANDSHAKE_REQ frame.
        seq = self._next_seq()
        payload = encode_handshake_req()
        frame = bytes([0x00]) + encode_frame(MessageType.HANDSHAKE_REQ, seq, payload)

        self._pending_handshake = None
        self._event.clear()
        self._transport.write(frame)

        # Wait for HANDSHAKE_RESP.
        self._event.wait(timeout)
        if self._pending_handshake is not None:
            resp = decode_handshake_resp(self._pending_handshake.payload)
            if resp and resp[1] == HANDSHAKE_MAGIC:
                self._connected = True
                return True

        return False

    def disconnect(self) -> None:
        """Send EXIT_HANDSHAKE and close the transport."""
        if self._connected:
            seq = self._next_seq()
            frame = encode_frame(MessageType.EXIT_HANDSHAKE, seq)
            try:
                self._transport.write(frame)
                time.sleep(0.05)  # Allow device to process.
            except OSError:
                pass
            self._connected = False

        self._running = False
        if self._reader_thread:
            self._reader_thread.join(timeout=1.0)
            self._reader_thread = None
        self._transport.disconnect()

    @property
    def is_connected(self) -> bool:
        """Whether FRAMED mode is active."""
        return self._connected

    # ─── Commands ─────────────────────────────────────────────────────

    def send_command(
        self, cmd_id: int, device: int, value: float, timeout: float = 2.0
    ) -> Optional[DecodedFrame]:
        """Send a COMMAND frame and wait for ACK/NACK.

        Returns the response frame, or None on timeout.
        """
        seq = self._next_seq()
        payload = encode_command(cmd_id, device, value)
        frame = encode_frame(MessageType.COMMAND, seq, payload)

        with self._lock:
            self._pending_ack = None
            self._event.clear()

        self._transport.write(frame)
        self._event.wait(timeout)
        return self._pending_ack

    def request_metrics(self, timeout: float = 2.0) -> Optional[bytes]:
        """Send METRICS_REQ and wait for METRICS_RESP payload.

        Returns the raw metrics payload bytes, or None on timeout.
        """
        seq = self._next_seq()
        frame = encode_frame(MessageType.METRICS_REQ, seq)

        with self._lock:
            self._pending_metrics = None
            self._event.clear()

        self._transport.write(frame)
        self._event.wait(timeout)
        if self._pending_metrics:
            return self._pending_metrics.payload
        return None

    def send_heartbeat(self) -> None:
        """Send a HEARTBEAT frame (no response expected)."""
        seq = self._next_seq()
        frame = encode_frame(MessageType.HEARTBEAT, seq)
        self._transport.write(frame)

    # ─── Subscriptions ────────────────────────────────────────────────

    def on_telemetry(self, callback: Callable[[bytes], None]) -> None:
        """Register callback for TELEMETRY messages."""
        self._on_telemetry = callback

    def on_data(self, callback: Callable[[bytes], None]) -> None:
        """Register callback for DATA messages."""
        self._on_data = callback

    def on_log(self, callback: Callable[[int, str], None]) -> None:
        """Register callback for LOG messages."""
        self._on_log = callback

    # ─── Internal ─────────────────────────────────────────────────────

    def _next_seq(self) -> int:
        """Return the next sequence number (wrapping 0–255)."""
        seq = self._seq
        self._seq = (self._seq + 1) & 0xFF
        return seq

    def _reader_loop(self) -> None:
        """Background thread: read bytes and feed to decoder."""
        while self._running:
            try:
                data = self._transport.read(1024)
                if data:
                    self._decoder.feed(data)
                else:
                    time.sleep(0.01)
            except OSError:
                break

    def _on_frame(self, frame: DecodedFrame) -> None:
        """Dispatch a decoded frame to the appropriate handler."""
        mt = frame.msg_type

        if mt in (MessageType.ACK, MessageType.NACK):
            with self._lock:
                self._pending_ack = frame
            self._event.set()

        elif mt == MessageType.HANDSHAKE_RESP:
            with self._lock:
                self._pending_handshake = frame
            self._event.set()

        elif mt == MessageType.METRICS_RESP:
            with self._lock:
                self._pending_metrics = frame
            self._event.set()

        elif mt == MessageType.TELEMETRY:
            if self._on_telemetry:
                self._on_telemetry(frame.payload)

        elif mt == MessageType.DATA:
            if self._on_data:
                self._on_data(frame.payload)

        elif mt == MessageType.LOG:
            if self._on_log:
                result = decode_log(frame.payload)
                if result:
                    self._on_log(result[0], result[1])

        elif mt == MessageType.HEARTBEAT:
            pass  # Heartbeat echo — no action needed.
