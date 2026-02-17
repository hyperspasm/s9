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
