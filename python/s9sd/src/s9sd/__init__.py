import asyncio
from StreamDeck.DeviceManager import DeviceManager
from StreamDeck.Devices.StreamDeck import DialEventType
from pn.client import Client
from pn.transport import connect_uds_persistent

SOCKET_PATH = "/tmp/s9.sock"

DIALS = [
    {"path": "s9/osc/time_per_div", "mode": "log",    "factor": 1/1.1},
    {"path": "s9/osc/amp_per_div",  "mode": "log",    "factor": 1/1.1},
    {"path": "s9/osc/trig_thresh",  "mode": "linear", "step": 0.02},
]

DEFAULTS = {
    "s9/osc/time_per_div": 0.002,
    "s9/osc/amp_per_div": 0.1,
    "s9/osc/trig_thresh": 0.1,
}


async def _main():
    loop = asyncio.get_event_loop()
    state = dict(DEFAULTS)

    def on_update(path, value, ptype):
        if path in state:
            state[path] = value

    def on_connect():
        print(f"pn: connected to {SOCKET_PATH}")
        for path in state:
            client.get(path)

    def on_disconnect():
        print("pn: disconnected, will retry...")

    client = Client(on_update=on_update)
    task, close = await connect_uds_persistent(
        client, SOCKET_PATH,
        on_connect=on_connect,
        on_disconnect=on_disconnect,
    )

    def on_dial(deck, dial_index, event, value):
        if event != DialEventType.TURN or dial_index >= len(DIALS):
            return

        dial = DIALS[dial_index]
        path = dial["path"]
        current = state[path]

        if dial["mode"] == "log":
            new_val = current * (dial["factor"] ** value)
        else:
            new_val = current + dial["step"] * value

        state[path] = new_val
        loop.call_soon_threadsafe(client.set_float, path, new_val)
        print(f"{path} = {new_val:.6f}")

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
    print(f"dials: {', '.join(d['path'].split('/')[-1] for d in DIALS)}")

    try:
        await asyncio.sleep(float("inf"))
    except KeyboardInterrupt:
        pass
    finally:
        deck.close()
        close()
        await task


def main():
    try:
        asyncio.run(_main())
    except KeyboardInterrupt:
        pass
