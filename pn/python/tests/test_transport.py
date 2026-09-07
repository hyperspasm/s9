"""Websocket transport: a hub server, clients that connect once or keep
reconnecting, and the subprotocol requirement."""
import asyncio
import socket

import pytest
import websockets

import pn
from pn import transport
from pn._types import ParamType


class Root:
    gain = pn.Float(default=0.0, min=-72.0, max=6.0, step=0.1)
    mute = pn.Bool(default=False)


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


async def serve(port):
    """A pn server over websockets, as a task; cancel it to stop."""
    server = pn.Server(Root(), name="root")
    task = asyncio.ensure_future(transport.serve_ws(server, "127.0.0.1", port))
    for _ in range(100):
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                return server, task
        except OSError:
            await asyncio.sleep(0.02)
    task.cancel()
    raise RuntimeError("server did not start")


async def stop(task):
    task.cancel()
    try:
        await task
    except asyncio.CancelledError:
        pass


async def test_ws_set_get_and_requests():
    port = free_port()
    server, srv = await serve(port)
    updates = asyncio.Queue()
    client = pn.Client(on_update=lambda p, v, t: updates.put_nowait((p, v, t)))
    _, close = await transport.connect_ws(client, f"ws://127.0.0.1:{port}")
    try:
        client.set_float("root/gain", -6.0)
        assert await asyncio.wait_for(updates.get(), 2) == ("root/gain", -6.0, ParamType.FLOAT)
        client.get("root/mute")
        assert await asyncio.wait_for(updates.get(), 2) == ("root/mute", False, ParamType.BOOL)
        assert await client.request_schema("root") == (server.schema_hash, "root")
        assert await client.request_manifest("root") == server.manifest.encode("utf-8")
        assert await client.check_schema(server.manifest) is True
    finally:
        await close()
        await stop(srv)


async def test_ws_hub_broadcasts_to_every_client():
    """A change requested by one client is echoed to every client."""
    port = free_port()
    _, srv = await serve(port)
    got = [asyncio.Queue(), asyncio.Queue()]
    clients = [pn.Client(on_update=lambda p, v, t, q=q: q.put_nowait((p, v))) for q in got]
    closes = [(await transport.connect_ws(c, f"ws://127.0.0.1:{port}"))[1] for c in clients]
    try:
        clients[0].set_bool("root/mute", True)
        for q in got:
            assert await asyncio.wait_for(q.get(), 2) == ("root/mute", True)
    finally:
        for close in closes:
            await close()
        await stop(srv)


async def test_ws_requires_subprotocol():
    """A client that does not request the subprotocol is refused at the handshake."""
    port = free_port()
    _, srv = await serve(port)
    try:
        with pytest.raises(websockets.exceptions.InvalidStatus) as refused:
            await websockets.connect(f"ws://127.0.0.1:{port}")
        assert refused.value.response.status_code == 400
    finally:
        await stop(srv)


async def test_ws_persistent_reconnects():
    """The persistent client connects when the server appears, notices when it
    goes, and connects again when it returns; a send while disconnected is
    dropped."""
    port = free_port()
    events = asyncio.Queue()
    updates = asyncio.Queue()
    client = pn.Client(on_update=lambda p, v, t: updates.put_nowait(v))
    task, close = await transport.connect_ws_persistent(
        client, f"ws://127.0.0.1:{port}", retry_interval=0.05,
        on_connect=lambda: events.put_nowait("connect"),
        on_disconnect=lambda: events.put_nowait("disconnect"))
    try:
        client.set_float("root/gain", 1.0)   # nobody there: dropped
        await asyncio.sleep(0.15)
        assert events.empty()

        _, srv = await serve(port)
        assert await asyncio.wait_for(events.get(), 2) == "connect"
        client.set_float("root/gain", 2.0)
        assert await asyncio.wait_for(updates.get(), 2) == 2.0

        await stop(srv)
        assert await asyncio.wait_for(events.get(), 2) == "disconnect"

        _, srv = await serve(port)
        assert await asyncio.wait_for(events.get(), 2) == "connect"
        client.set_float("root/gain", 3.0)
        assert await asyncio.wait_for(updates.get(), 2) == 3.0
        await stop(srv)
    finally:
        close()
        try:
            await task
        except asyncio.CancelledError:
            pass
