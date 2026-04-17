# sputterctl

Command-line tool for communicating with SputterOS devices over serial or TCP.

## Installation

```bash
cd tools/cli
pip install -e ".[dev]"
```

## Usage

### Interactive REPL (serial)

```bash
sputterctl repl --port /dev/ttyACM0 --baud 115200
```

### Interactive REPL (TCP loopback)

```bash
sputterctl repl --tcp localhost:9000
```

### Send a single command

```bash
sputterctl send --port /dev/ttyACM0 1 0 50.0
```

### Monitor performance metrics

```bash
sputterctl monitor --port /dev/ttyACM0 --interval 2
```

## REPL Commands

| Command                         | Description                    |
|---------------------------------|--------------------------------|
| `send <cmd_id> <device> <val>`  | Send a command                 |
| `metrics`                       | Request performance snapshot   |
| `monitor`                       | Continuous metrics display     |
| `mode`                          | Show current comms mode        |
| `help`                          | List available commands        |
| `quit` / `exit`                 | Disconnect and exit            |

## Protocol

The tool negotiates FRAMED mode with the device using COBS-encoded binary
frames over a zero-byte-delimited stream. See `docs/CommsProtocol.md` for
the full protocol specification.

On startup, sputterctl sends a HANDSHAKE_REQ frame (with a leading 0x00
sync byte). The device responds with HANDSHAKE_RESP to confirm FRAMED mode.
On exit, sputterctl sends EXIT_HANDSHAKE so the device reverts to TEXT mode
for human terminal use.
