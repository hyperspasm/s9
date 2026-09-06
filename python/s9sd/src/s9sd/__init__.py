"""Stream Deck dial controller for s9: each dial drives one s9 parameter over pn."""
import argparse
import asyncio
import sys
import warnings
from pathlib import Path

from StreamDeck.DeviceManager import DeviceManager
from StreamDeck.Devices.StreamDeck import DialEventType
from pn import manifest, SchemaMismatchWarning
from pn.client import Client
from pn.transport import connect_uds_persistent

SOCKET_PATH = "/tmp/s9.sock"

# The committed s9 manifest, located relative to this file in a source checkout
MANIFEST_PATH = Path(__file__).resolve().parents[4] / "s9.manifest"

# What each dial controls and how it moves. The range comes from the manifest.
DIALS = [
    {"path": "s9/osc/time_per_div", "mode": "log",    "factor": 1/1.1},
    {"path": "s9/osc/amp_per_div",  "mode": "log",    "factor": 1/1.1},
    {"path": "s9/osc/trig_thresh",  "mode": "linear", "step": 0.02},
    {"path": "s9/osc/channel",      "mode": "int",    "step": 1},
]

def load_dials(manifest_path):
    """Read the manifest and return the dials with their ranges filled in."""
    try:
        m = manifest.parse(Path(manifest_path).read_bytes())
    except OSError as e:
        sys.exit(f"{manifest_path}: {e.strerror}")
    except manifest.ManifestError as e:
        sys.exit(f"{manifest_path}: {e}")
    dials = []
    for spec in DIALS:
        entry = m.by_path.get(spec["path"])
        if entry is None:
            sys.exit(f"{manifest_path}: no entry for {spec['path']}")
        dials.append({**spec, "min": entry.min, "max": entry.max})
    return m, dials


async def _main(manifest_path):
    loop = asyncio.get_event_loop()
    m, dials = load_dials(manifest_path)
    # The server's value of each dial's parameter, or None until it has reported one.
    # A dial does nothing while its value is unknown, so it never moves from a guess.
    state = {d["path"]: None for d in dials}

    # A mismatch is reported on every reconnect, not only the first
    warnings.simplefilter("always", SchemaMismatchWarning)

    def on_update(path, value, ptype):
        if path in state:
            state[path] = value
            print(f"{path} = {value}")

    async def on_connected():
        # Warn if the server's schema differs from the manifest, then carry on:
        # the dials only touch parameters they know
        try:
            await client.check_schema(m)
        except asyncio.TimeoutError:
            print("pn: server did not report a schema hash")
        for path in state:
            client.get(path)

    def on_connect():
        print(f"pn: connected to {SOCKET_PATH}")
        asyncio.ensure_future(on_connected())

    def on_disconnect():
        print("pn: disconnected, will retry...")
        for path in state:
            state[path] = None

    client = Client(on_update=on_update)
    task, close = await connect_uds_persistent(
        client, SOCKET_PATH,
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
    print(f"connected: {deck.deck_type()}")
    print(f"dials: {', '.join(d['path'].split('/')[-1] for d in dials)}")

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
                        help="manifest to take the parameter ranges from "
                             "(default: the s9.manifest of this checkout)")
    args = parser.parse_args()
    try:
        asyncio.run(_main(args.manifest))
    except KeyboardInterrupt:
        pass
