import asyncio
import os


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
