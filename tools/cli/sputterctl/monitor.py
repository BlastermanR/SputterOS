"""Live performance metrics monitor."""

from __future__ import annotations

import time

from .client import SputterClient


def run_monitor(client: SputterClient, interval: float = 2.0) -> None:
    """Periodically request and display metrics.

    Runs until interrupted with Ctrl-C.
    """
    print(f"Monitoring metrics every {interval:.1f}s — Ctrl-C to stop.")
    print()

    try:
        while client.is_connected:
            data = client.request_metrics(timeout=interval)
            if data is None:
                print("[timeout] No metrics response.")
            elif len(data) == 0:
                print("[empty] Device sent empty metrics.")
            else:
                _display_metrics(data)
            time.sleep(interval)
    except KeyboardInterrupt:
        print("\nMonitor stopped.")


def _display_metrics(data: bytes) -> None:
    """Display raw metrics payload as a hex dump.

    TODO: Deserialize PerformanceSnapshot fields when the struct
    layout is finalized in the C++ library.
    """
    timestamp = time.strftime("%H:%M:%S")
    print(f"[{timestamp}] Metrics ({len(data)} bytes):")
    for i in range(0, len(data), 16):
        chunk = data[i : i + 16]
        hex_str = " ".join(f"{b:02x}" for b in chunk)
        ascii_str = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        print(f"  {i:04x}: {hex_str:<48s} {ascii_str}")
    print()
