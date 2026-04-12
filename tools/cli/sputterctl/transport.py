"""Transport abstraction layer — serial and TCP backends.

Both transports share the same interface so the upper layers
(:class:`SputterClient`, REPL, monitor) are transport-agnostic.
"""

from __future__ import annotations

import socket
import time
from abc import ABC, abstractmethod
from typing import Optional

try:
    import serial  # type: ignore[import-untyped]
except ImportError:  # pragma: no cover
    serial = None  # Allows TCP-only usage without pyserial.


class Transport(ABC):
    """Abstract base for bidirectional byte transport."""

    @abstractmethod
    def connect(self) -> None:
        """Open the transport."""
        ...

    @abstractmethod
    def disconnect(self) -> None:
        """Close the transport."""
        ...

    @abstractmethod
    def read(self, max_bytes: int = 1024) -> bytes:
        """Non-blocking read up to *max_bytes*. Return empty bytes if nothing available."""
        ...

    @abstractmethod
    def write(self, data: bytes) -> None:
        """Blocking write."""
        ...

    @abstractmethod
    def is_connected(self) -> bool:
        """Return True if the transport is open."""
        ...


class SerialTransport(Transport):
    """pyserial transport for USB CDC / UART connections."""

    def __init__(self, port: str, baudrate: int = 115200, timeout: float = 0.1) -> None:
        if serial is None:
            raise ImportError("pyserial is required for serial transport (pip install pyserial)")
        self._port = port
        self._baudrate = baudrate
        self._timeout = timeout
        self._ser: Optional[serial.Serial] = None

    def connect(self) -> None:
        self._ser = serial.Serial(self._port, self._baudrate, timeout=self._timeout)
        # Give the device time to reset after connection.
        time.sleep(0.1)

    def disconnect(self) -> None:
        if self._ser and self._ser.is_open:
            self._ser.close()
        self._ser = None

    def read(self, max_bytes: int = 1024) -> bytes:
        if not self._ser:
            return b""
        waiting = self._ser.in_waiting
        if waiting == 0:
            return b""
        return self._ser.read(min(waiting, max_bytes))

    def write(self, data: bytes) -> None:
        if self._ser:
            self._ser.write(data)

    def is_connected(self) -> bool:
        return self._ser is not None and self._ser.is_open


class TcpTransport(Transport):
    """TCP socket transport for local loopback testing."""

    def __init__(self, host: str = "localhost", port: int = 9000, timeout: float = 0.1) -> None:
        self._host = host
        self._port = port
        self._timeout = timeout
        self._sock: Optional[socket.socket] = None

    def connect(self) -> None:
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.settimeout(self._timeout)
        self._sock.connect((self._host, self._port))

    def disconnect(self) -> None:
        if self._sock:
            try:
                self._sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self._sock.close()
        self._sock = None

    def read(self, max_bytes: int = 1024) -> bytes:
        if not self._sock:
            return b""
        try:
            return self._sock.recv(max_bytes)
        except (socket.timeout, BlockingIOError):
            return b""
        except OSError:
            return b""

    def write(self, data: bytes) -> None:
        if self._sock:
            self._sock.sendall(data)

    def is_connected(self) -> bool:
        return self._sock is not None
