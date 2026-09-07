"""A pn server over websockets for the JavaScript tests: the amp schema from
MANIFEST.md, on the port given. Prints "ready" once listening."""
import asyncio
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "python" / "src"))
import pn
from pn import transport


class Eq:
    bass = pn.Float(min=-12.0, max=12.0, step=0.5)
    treble = pn.Float(min=-12.0, max=12.0, step=0.5)


class Amp:
    power = pn.Bool()
    volume = pn.Int(min=0, max=100, step=1)
    eq = pn.Group(Eq)
    input = pn.Enum(labels=["mic", "line", "aux"])
    name = pn.String(max_len=32)

    @pn.Action
    def reset(self):
        self.volume = 0


async def main():
    server = pn.Server(Amp(), name="amp")
    task = asyncio.ensure_future(transport.serve_ws(server, "127.0.0.1", int(sys.argv[1])))
    await asyncio.sleep(0.1)
    print("ready", flush=True)
    await task


asyncio.run(main())
