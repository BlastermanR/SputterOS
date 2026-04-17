"""sputterctl CLI entry point.

Usage:
    sputterctl repl  --port /dev/ttyACM0 [--baud 115200]
    sputterctl repl  --tcp localhost:9000
    sputterctl send  --port /dev/ttyACM0 <cmd_id> <device> <value>
    sputterctl monitor --port /dev/ttyACM0 [--interval 2]
"""

from __future__ import annotations

import sys

import click

from .client import SputterClient
from .monitor import run_monitor
from .protocol import MessageType, decode_ack, decode_nack
from .repl import run_repl
from .transport import SerialTransport, TcpTransport, Transport


def _make_transport(port: str | None, tcp: str | None, baud: int) -> Transport:
    """Create a transport from CLI options."""
    if tcp:
        parts = tcp.split(":")
        host = parts[0] if parts[0] else "localhost"
        tcp_port = int(parts[1]) if len(parts) > 1 else 9000
        return TcpTransport(host, tcp_port)
    if port:
        return SerialTransport(port, baud)
    click.echo("Error: specify --port or --tcp", err=True)
    sys.exit(1)


@click.group()
def main() -> None:
    """SputterOS command-line interface."""
    pass


@main.command()
@click.option("--port", "-p", help="Serial port (e.g. /dev/ttyACM0, COM3)")
@click.option("--tcp", "-t", help="TCP address (host:port)")
@click.option("--baud", "-b", default=115200, help="Serial baud rate")
def repl(port: str | None, tcp: str | None, baud: int) -> None:
    """Interactive command REPL."""
    transport = _make_transport(port, tcp, baud)
    client = SputterClient(transport)

    click.echo("Connecting...")
    if not client.connect():
        click.echo("Handshake failed — is the device running?", err=True)
        sys.exit(1)
    click.echo("Connected (FRAMED mode).")

    try:
        run_repl(client)
    finally:
        client.disconnect()
        click.echo("Disconnected.")


@main.command()
@click.option("--port", "-p", help="Serial port")
@click.option("--tcp", "-t", help="TCP address (host:port)")
@click.option("--baud", "-b", default=115200, help="Serial baud rate")
@click.argument("cmd_id", type=int)
@click.argument("device", type=int)
@click.argument("value", type=float)
def send(port: str | None, tcp: str | None, baud: int, cmd_id: int, device: int, value: float) -> None:
    """Send a single command and display the response."""
    transport = _make_transport(port, tcp, baud)
    client = SputterClient(transport)

    if not client.connect():
        click.echo("Handshake failed.", err=True)
        sys.exit(1)

    resp = client.send_command(cmd_id, device, value)
    if resp is None:
        click.echo("Timeout — no response.")
    elif resp.msg_type == MessageType.ACK:
        ack_id = decode_ack(resp.payload)
        click.echo(f"ACK (cmd_id={ack_id})")
    elif resp.msg_type == MessageType.NACK:
        nack = decode_nack(resp.payload)
        if nack:
            click.echo(f"NACK (cmd_id={nack[0]}, device={nack[1]}, value={nack[2]})")
        else:
            click.echo("NACK (malformed payload)")

    client.disconnect()


@main.command()
@click.option("--port", "-p", help="Serial port")
@click.option("--tcp", "-t", help="TCP address (host:port)")
@click.option("--baud", "-b", default=115200, help="Serial baud rate")
@click.option("--interval", "-i", default=2.0, help="Polling interval (seconds)")
def monitor(port: str | None, tcp: str | None, baud: int, interval: float) -> None:
    """Live performance metrics monitor."""
    transport = _make_transport(port, tcp, baud)
    client = SputterClient(transport)

    if not client.connect():
        click.echo("Handshake failed.", err=True)
        sys.exit(1)

    try:
        run_monitor(client, interval)
    finally:
        client.disconnect()


if __name__ == "__main__":
    main()
