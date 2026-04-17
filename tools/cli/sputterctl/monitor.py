"""Live performance metrics monitor."""

from __future__ import annotations

import time

from .client import SputterClient
from .protocol import PerformanceMetrics, decode_metrics


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
                metrics = decode_metrics(data)
                if metrics is None:
                    print(f"[error] Failed to decode metrics ({len(data)} bytes)")
                else:
                    _display_metrics(metrics)
            time.sleep(interval)
    except KeyboardInterrupt:
        print("\nMonitor stopped.")


def _display_metrics(m: PerformanceMetrics) -> None:
    """Display decoded performance metrics in a human-readable table."""
    ts = time.strftime("%H:%M:%S")
    elapsed_s = m.timestamp / 1_000_000.0

    print(f"[{ts}] Metrics (uptime {elapsed_s:.1f}s, {m.scheduler_tick_count} ticks)")

    # Core utilization
    cores = "  ".join(
        f"Core {i}: {m.core_utilization[i]*100:.1f}%"
        for i in range(m.core_count)
    )
    print(f"  Cores:     {cores}")

    # Queue
    print(f"  Queue:     depth={m.queue_depth}  max={m.queue_max_depth}  avg={m.queue_avg_depth:.1f}")

    # Memory
    print(f"  Memory:    heap_peak={m.peak_heap_used}  free={m.free_heap}  stack_hw={m.stack_high_water}")

    # Scheduler health
    print(f"  Scheduler: gap_max={m.max_gap_us}us  gap_avg={m.avg_gap_us:.0f}us  "
          f"overruns={m.total_overruns}  misses={m.total_deadline_misses}")

    # Per-task table
    if m.tasks:
        print(f"  Tasks ({len(m.tasks)}):")
        print(f"    {'Idx':>3} {'Core':>4} {'Last':>8} {'Min':>8} {'Max':>8} {'Avg':>8} {'Samples':>8} {'Ovr':>4} {'Miss':>4}")
        for t in m.tasks:
            print(f"    {t.task_index:3d} {t.core_id:4d} {t.last_us:8d} {t.min_us:8d} {t.max_us:8d} "
                  f"{t.avg_us:8.0f} {t.samples:8d} {t.overruns:4d} {t.misses:4d}")
    print()
