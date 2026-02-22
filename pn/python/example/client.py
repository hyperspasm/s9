#!/usr/bin/env python3
"""Interactive pn client — connects to a pn server over UDS."""

import asyncio
import signal
import sys

import pn
from pn import transport, ParamType

SOCKET_PATH = "/tmp/pn_example.sock"


def on_update(path, value, ptype):
    if ptype == ParamType.BOOL:
        print(f"  {path} = {'true' if value else 'false'}")
    elif ptype == ParamType.INT:
        print(f"  {path} = {value}")
    elif ptype == ParamType.FLOAT:
        print(f"  {path} = {value:.4g}")
    elif ptype == ParamType.ENUM:
        print(f"  {path} = {value}")
    elif ptype == ParamType.STRING:
        print(f'  {path} = "{value}"')


def handle_line(client, line):
    line = line.strip()
    if not line:
        return

    # Action: !path
    if line.startswith("!"):
        client.action(line[1:])
        return

    # Split into path and optional value
    parts = line.split(None, 1)
    if len(parts) == 1:
        # Get
        client.get(parts[0])
        return

    path, val_str = parts

    # Bool
    if val_str == "true":
        client.set_bool(path, True)
        return
    if val_str == "false":
        client.set_bool(path, False)
        return

    # Float (contains '.')
    if "." in val_str:
        client.set_float(path, float(val_str))
        return

    # Int
    client.set_int(path, int(val_str))


async def stdin_reader():
    """Async generator yielding lines from stdin."""
    loop = asyncio.get_event_loop()
    reader = asyncio.StreamReader()
    await loop.connect_read_pipe(lambda: asyncio.StreamReaderProtocol(reader), sys.stdin)
    while True:
        line = await reader.readline()
        if not line:
            break
        yield line.decode()


async def main():
    client = pn.Client(on_update=on_update)

    try:
        read_task, close = await transport.connect_uds(client, SOCKET_PATH)
    except (ConnectionRefusedError, FileNotFoundError):
        print(f"failed to connect to {SOCKET_PATH}", file=sys.stderr)
        return 1

    print(f"connected to {SOCKET_PATH}")
    print("commands:")
    print("  path              get value")
    print("  path value        set value (auto-detects type)")
    print("  !path             trigger action")
    print("  quit              exit")
    print()

    try:
        print("> ", end="", flush=True)
        async for line in stdin_reader():
            if line.strip() == "quit":
                break
            handle_line(client, line)
            # Brief pause to let any server responses arrive and print
            await asyncio.sleep(0.05)
            print("> ", end="", flush=True)
    except asyncio.CancelledError:
        pass
    finally:
        close()
        print("\ndisconnecting")

    return 0


if __name__ == "__main__":
    loop = asyncio.get_event_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, loop.stop)
    try:
        rc = loop.run_until_complete(main())
    except RuntimeError:
        rc = 0
    sys.exit(rc or 0)
