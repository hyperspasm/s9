"""Transports: the host-side glue that moves pn's bytes over a stream.

Unix sockets carry the byte stream as is. Websockets carry one pn packet per
binary message, with the subprotocol "pn"; the server side is a hub, where
every client's packets feed the one server and everything the server sends
goes to every client.
"""
import asyncio
import os

import websockets
from websockets.exceptions import ConnectionClosed

SUBPROTOCOL = "pn"


# --- Unix sockets ---

async def serve_uds(server, path, *, loop=None):
    """Start a UDS server. Blocks until cancelled."""
    if os.path.exists(path):
        os.unlink(path)

    async def on_client(reader, writer):
        server.bind(lambda data: writer.write(data))
        try:
            while True:
                data = await reader.read(4096)
                if not data:
                    break
                server.feed(data)
        except asyncio.CancelledError:
            pass
        finally:
            writer.close()
            await writer.wait_closed()

    srv = await asyncio.start_unix_server(on_client, path=path)
    try:
        await srv.serve_forever()
    finally:
        srv.close()
        if os.path.exists(path):
            os.unlink(path)


async def connect_uds(client, path):
    """Connect a client to a UDS server. Returns (reader_task, close_fn)."""
    reader, writer = await asyncio.open_unix_connection(path)
    client.bind(lambda data: writer.write(data))

    async def read_loop():
        try:
            while True:
                data = await reader.read(4096)
                if not data:
                    break
                client.feed(data)
        except asyncio.CancelledError:
            pass

    task = asyncio.ensure_future(read_loop())

    def close():
        task.cancel()
        writer.close()

    return task, close


async def connect_uds_persistent(client, path, *,
                                  retry_interval=1.0,
                                  on_connect=None,
                                  on_disconnect=None):
    """Connect a client with auto-reconnection.

    Keeps trying to connect (or reconnect after disconnect) every
    retry_interval seconds.  Sends are silently dropped while disconnected.
    Returns (task, close_fn).
    """
    writer_ref = [None]

    def send(data):
        w = writer_ref[0]
        if w and not w.is_closing():
            try:
                w.write(data)
            except Exception:
                pass

    client.bind(send)

    async def run():
        while True:
            connected = False
            try:
                reader, writer = await asyncio.open_unix_connection(path)
                writer_ref[0] = writer
                connected = True
                if on_connect:
                    on_connect()
                while True:
                    data = await reader.read(4096)
                    if not data:
                        break
                    client.feed(data)
            except asyncio.CancelledError:
                return
            except (ConnectionRefusedError, FileNotFoundError, OSError):
                pass
            finally:
                if connected:
                    writer_ref[0] = None
                    writer.close()
                    if on_disconnect:
                        on_disconnect()

            try:
                await asyncio.sleep(retry_interval)
            except asyncio.CancelledError:
                return

    task = asyncio.ensure_future(run())

    def close():
        task.cancel()

    return task, close


# --- Websockets ---

class _Link:
    """One open websocket carrying pn packets. Packets to send go through a
    queue drained by one writer task, so they leave in the order given; each
    binary message received is fed whole to `feed`."""

    def __init__(self, ws, feed):
        self.ws = ws
        self.feed = feed
        self.queue = asyncio.Queue()
        self.writer = asyncio.ensure_future(self._write())

    async def _write(self):
        while True:
            await self.ws.send(await self.queue.get())

    def send(self, data):
        self.queue.put_nowait(data)

    async def read(self):
        """Feed received messages until the connection closes."""
        try:
            async for message in self.ws:
                if isinstance(message, bytes):
                    self.feed(message)
        except ConnectionClosed:
            pass
        finally:
            self.writer.cancel()

    async def close(self):
        self.writer.cancel()
        await self.ws.close()


async def serve_ws(server, host, port):
    """Serve a pn server over websockets on host:port. A client that does not
    request the subprotocol is refused at the handshake. Blocks until
    cancelled."""
    links = set()

    def broadcast(data):
        for link in list(links):
            link.send(data)

    server.bind(broadcast)

    async def handler(ws):
        link = _Link(ws, server.feed)
        links.add(link)
        try:
            await link.read()
        finally:
            links.discard(link)

    async with websockets.serve(handler, host, port, subprotocols=[SUBPROTOCOL]) as srv:
        await srv.serve_forever()


async def connect_ws(client, uri):
    """Connect a client to a websocket server. Returns (reader_task, close_fn);
    close_fn returns a future that completes when the connection has closed."""
    ws = await websockets.connect(uri, subprotocols=[SUBPROTOCOL])
    link = _Link(ws, client.feed)
    client.bind(link.send)
    task = asyncio.ensure_future(link.read())

    def close():
        task.cancel()
        return asyncio.ensure_future(link.close())

    return task, close


async def connect_ws_persistent(client, uri, *,
                                retry_interval=1.0,
                                on_connect=None,
                                on_disconnect=None):
    """Connect a client with auto-reconnection.

    Keeps trying to connect (or reconnect after disconnect) every
    retry_interval seconds. Sends are silently dropped while disconnected.
    Returns (task, close_fn).
    """
    current = [None]

    def send(data):
        link = current[0]
        if link is not None:
            link.send(data)

    client.bind(send)

    async def run():
        while True:
            try:
                ws = await websockets.connect(uri, subprotocols=[SUBPROTOCOL])
            except asyncio.CancelledError:
                return
            except Exception:
                ws = None
            if ws is not None:
                link = _Link(ws, client.feed)
                current[0] = link
                if on_connect:
                    on_connect()
                try:
                    await link.read()
                except asyncio.CancelledError:
                    current[0] = None
                    await link.close()
                    return
                finally:
                    current[0] = None
                if on_disconnect:
                    on_disconnect()
            try:
                await asyncio.sleep(retry_interval)
            except asyncio.CancelledError:
                return

    task = asyncio.ensure_future(run())

    def close():
        task.cancel()

    return task, close
