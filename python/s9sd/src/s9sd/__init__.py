"""Stream Deck controller for s9: the dials drive s9 parameters and the top
corner keys step the display mode, all over pn."""
import argparse
import asyncio
import sys
import warnings
from pathlib import Path

from StreamDeck.DeviceManager import DeviceManager
from StreamDeck.Devices.StreamDeck import DialEventType
from pn import manifest, SchemaMismatchWarning
from pn.client import Client
from pn.transport import connect_ws_persistent

# s9's websocket server on this machine, at s9's default port
DEFAULT_URL = "ws://127.0.0.1:8080"

# The committed s9 manifest, located relative to this file in a source checkout
MANIFEST_PATH = Path(__file__).resolve().parents[4] / "s9.manifest"

# What each dial controls and how it moves. The range comes from the manifest.
DIALS = [
    {"path": "s9/oscilloscope/time_per_div", "mode": "log",    "factor": 1/1.1},
    {"path": "s9/oscilloscope/amp_per_div",  "mode": "log",    "factor": 1/1.1},
    {"path": "s9/oscilloscope/trig_thresh",  "mode": "linear", "step": 0.02},
    {"path": "s9/oscilloscope/channel",      "mode": "int",    "step": 1},
]

# The display mode, stepped by the top-left (down) and top-right (up) keys.
# Its names come from the manifest.
MODE_PATH = "s9/mode"


def mode_step(key, cols):
    """The mode step a key means on a deck with cols columns: -1 for the
    top-left key, +1 for the top-right key, None for any other."""
    if key == 0:
        return -1
    if key == cols - 1:
        return 1
    return None


def load_manifest(manifest_path):
    """Read the manifest; return it, the dials with their ranges, and the mode names."""
    try:
        m = manifest.parse(Path(manifest_path).read_bytes())
    except OSError as e:
        sys.exit(f"{manifest_path}: {e.strerror}")
    except manifest.ManifestError as e:
        sys.exit(f"{manifest_path}: {e}")

    def entry(path):
        e = m.by_path.get(path)
        if e is None:
            sys.exit(f"{manifest_path}: no entry for {path}")
        return e

    dials = []
    for spec in DIALS:
        e = entry(spec["path"])
        dials.append({**spec, "min": e.min, "max": e.max})
    modes = entry(MODE_PATH).labels
    return m, dials, modes


async def _main(url, manifest_path):
    loop = asyncio.get_event_loop()
    m, dials, modes = load_manifest(manifest_path)
    # The server's value of each parameter, or None until it has reported one.
    # A dial or key does nothing while its value is unknown, so it never moves
    # from a guess.
    state = {d["path"]: None for d in dials}
    state[MODE_PATH] = None

    # A mismatch is reported on every reconnect, not only the first
    warnings.simplefilter("always", SchemaMismatchWarning)

    def on_update(path, value, ptype):
        if path in state:
            state[path] = value
            shown = modes[value] if path == MODE_PATH and value < len(modes) else value
            print(f"{path} = {shown}")

    async def on_connected():
        # Warn if the server's schema differs from the manifest, then carry on:
        # the controls only touch parameters they know
        try:
            await client.check_schema(m)
        except asyncio.TimeoutError:
            print("pn: server did not report a schema hash")
        for path in state:
            client.get(path)

    def on_connect():
        print(f"pn: connected to {url}")
        asyncio.ensure_future(on_connected())

    def on_disconnect():
        print("pn: disconnected, will retry...")
        for path in state:
            state[path] = None

    client = Client(on_update=on_update)
    task, close = await connect_ws_persistent(
        client, url,
        on_connect=on_connect,
        on_disconnect=on_disconnect,
    )

    def on_dial(deck, dial_index, event, value):
        if event != DialEventType.TURN or dial_index >= len(dials):
            return

        dial = dials[dial_index]
        path = dial["path"]
        current = state[path]
        if current is None:
            return

        if dial["mode"] == "log":
            new_val = current * (dial["factor"] ** value)
        elif dial["mode"] == "int":
            new_val = int(current) + dial["step"] * value
        else:
            new_val = current + dial["step"] * value

        new_val = max(dial["min"], min(dial["max"], new_val))
        state[path] = new_val
        if dial["mode"] == "int":
            loop.call_soon_threadsafe(client.set_int, path, new_val)
        else:
            loop.call_soon_threadsafe(client.set_float, path, new_val)

    def on_key(deck, key, pressed):
        if not pressed:
            return
        _, cols = deck.key_layout()
        step = mode_step(key, cols)
        current = state[MODE_PATH]
        if step is None or current is None:
            return
        new_val = (current + step) % len(modes)
        state[MODE_PATH] = new_val
        loop.call_soon_threadsafe(client.set_enum, MODE_PATH, new_val)

    manager = DeviceManager()
    decks = manager.enumerate()
    deck = next((d for d in decks if hasattr(d, "DIAL_COUNT") and d.DIAL_COUNT > 0), None)
    if not deck:
        print("no stream deck with dials found")
        close()
        await task
        return

    deck.open()
    deck.set_dial_callback(on_dial)
    deck.set_key_callback(on_key)
    print(f"connected: {deck.deck_type()}")
    print(f"dials: {', '.join(d['path'].split('/')[-1] for d in dials)}")
    print(f"top-left/top-right keys: mode down/up through {', '.join(modes)}")

    try:
        await asyncio.sleep(float("inf"))
    except KeyboardInterrupt:
        pass
    finally:
        deck.close()
        close()
        await task


def main():
    parser = argparse.ArgumentParser(prog="s9sd", description=__doc__)
    parser.add_argument("manifest", nargs="?", default=MANIFEST_PATH,
                        help="manifest to take the parameter ranges and mode names from "
                             "(default: the s9.manifest of this checkout)")
    parser.add_argument("--url", default=DEFAULT_URL,
                        help=f"websocket URL of s9 (default: {DEFAULT_URL})")
    args = parser.parse_args()
    try:
        asyncio.run(_main(args.url, args.manifest))
    except KeyboardInterrupt:
        pass
