"""Tests for the transport abstraction layer.

Tests TcpTransport by starting a localhost server and verifying
connect / send / receive / disconnect lifecycle.
"""

import socket
import threading
import time

import pytest

from sputterctl.transport import TcpTransport, Transport


# ── Abstract base behaviour ──────────────────────────────────────────


class StubTransport(Transport):
    """Minimal concrete subclass for testing the ABC."""

    def __init__(self):
        self._open = False
        self._written: bytes = b""

    def connect(self) -> None:
        self._open = True

    def disconnect(self) -> None:
        self._open = False

    def write(self, data: bytes) -> None:
        self._written += data

    def read(self, max_bytes: int = 1024) -> bytes:
        return b""

    def is_connected(self) -> bool:
        return self._open


class TestStubTransport:
    def test_lifecycle(self):
        t = StubTransport()
        assert not t.is_connected()
        t.connect()
        assert t.is_connected()
        t.write(b"\x01\x02")
        assert t._written == b"\x01\x02"
        t.disconnect()
        assert not t.is_connected()


# ── TcpTransport tests ──────────────────────────────────────────────


def _echo_server(host: str, port: int, ready: threading.Event, stop: threading.Event):
    """Simple echo server: accepts one client and echoes data back."""
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((host, port))
    srv.listen(1)
    srv.settimeout(5.0)
    ready.set()
    try:
        conn, _ = srv.accept()
        conn.settimeout(1.0)
        while not stop.is_set():
            try:
                data = conn.recv(1024)
                if not data:
                    break
                conn.sendall(data)
            except socket.timeout:
                continue
        conn.close()
    except socket.timeout:
        pass
    finally:
        srv.close()


@pytest.fixture()
def echo_server():
    """Start a TCP echo server on localhost and yield (host, port)."""
    host = "127.0.0.1"
    ready = threading.Event()
    stop = threading.Event()

    # Bind to port 0 to let OS pick a free port, then read it.
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((host, 0))
    port = srv.getsockname()[1]
    srv.close()

    t = threading.Thread(target=_echo_server, args=(host, port, ready, stop), daemon=True)
    t.start()
    ready.wait(timeout=5)

    yield host, port

    stop.set()
    t.join(timeout=5)


class TestTcpTransport:
    def test_send_receive(self, echo_server):
        host, port = echo_server
        transport = TcpTransport(host, port)
        transport.connect()
        assert transport.is_connected()

        transport.write(b"\x01\x02\x03")
        time.sleep(0.1)  # Let echo server respond.
        data = transport.read(3)
        assert data == b"\x01\x02\x03"

        transport.disconnect()
        assert not transport.is_connected()

    def test_read_no_data(self, echo_server):
        """Read with no pending data should return empty."""
        host, port = echo_server
        transport = TcpTransport(host, port, timeout=0.1)
        transport.connect()

        data = transport.read(1)
        assert data == b""

        transport.disconnect()

    def test_connect_refused(self):
        """Connecting to a closed port should raise."""
        transport = TcpTransport("127.0.0.1", 1)  # Port 1 almost certainly closed.
        with pytest.raises(Exception):
            transport.connect()
