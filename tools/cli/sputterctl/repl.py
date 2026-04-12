"""Interactive REPL for SputterOS comms."""

from __future__ import annotations

from typing import Optional

from .client import SputterClient
from .protocol import MessageType, decode_ack, decode_nack


def run_repl(client: SputterClient) -> None:
    """Run an interactive command loop.

    Assumes the client is already connected (FRAMED mode).
    """
    # Register log display callback.
    client.on_log(lambda level, text: print(f"[LOG:{level}] {text}"))

    print("SputterOS REPL — type 'help' for commands, 'quit' to exit.")
    print()

    while True:
        try:
            line = input("sputterctl> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            break

        if not line:
            continue

        parts = line.split()
        cmd = parts[0].lower()

        if cmd in ("quit", "exit"):
            break
        elif cmd == "help":
            _print_help()
        elif cmd == "send":
            _handle_send(client, parts[1:])
        elif cmd == "metrics":
            _handle_metrics(client)
        elif cmd == "mode":
            print(f"Mode: {'FRAMED' if client.is_connected else 'DISCONNECTED'}")
        elif cmd == "heartbeat":
            client.send_heartbeat()
            print("Heartbeat sent.")
        else:
            print(f"Unknown command: {cmd}. Type 'help' for options.")


def _print_help() -> None:
    print("Commands:")
    print("  send <cmd_id> <device> <value>  — Send a command")
    print("  metrics                         — Request performance snapshot")
    print("  heartbeat                       — Send heartbeat")
    print("  mode                            — Show connection state")
    print("  help                            — Show this help")
    print("  quit / exit                     — Disconnect and exit")


def _handle_send(client: SputterClient, args: list[str]) -> None:
    if len(args) != 3:
        print("Usage: send <cmd_id> <device> <value>")
        return

    try:
        cmd_id = int(args[0])
        device = int(args[1])
        value = float(args[2])
    except ValueError:
        print("Error: cmd_id and device must be integers, value must be a number.")
        return

    resp = client.send_command(cmd_id, device, value)
    if resp is None:
        print("Timeout — no response from device.")
        return

    if resp.msg_type == MessageType.ACK:
        ack_id = decode_ack(resp.payload)
        print(f"ACK (cmd_id={ack_id})")
    elif resp.msg_type == MessageType.NACK:
        nack = decode_nack(resp.payload)
        if nack:
            print(f"NACK (cmd_id={nack[0]}, device={nack[1]}, value={nack[2]})")
        else:
            print("NACK (malformed payload)")
    else:
        print(f"Unexpected response: {resp.msg_type.name}")


def _handle_metrics(client: SputterClient) -> None:
    data = client.request_metrics()
    if data is None:
        print("Timeout — no metrics response.")
        return
    if len(data) == 0:
        print("Metrics: (empty — device may not support full snapshot)")
        return
    print(f"Metrics payload: {len(data)} bytes")
    # Pretty-print as hex dump for now.
    for i in range(0, len(data), 16):
        chunk = data[i : i + 16]
        hex_str = " ".join(f"{b:02x}" for b in chunk)
        print(f"  {i:04x}: {hex_str}")
