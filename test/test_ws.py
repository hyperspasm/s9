"""Integration test of the websocket hub against test_ws_server.

The hub's contract: binary messages ending in the frame delimiter are broadcast
to every client including the sender, in order, and reassembled from fragments;
messages without the delimiter, over the size limit, or not binary are dropped;
a client connecting after a message was queued does not receive it; a client
whose requests fill the queue is paused instead of losing replies, while a
single request producing more replies than the queue holds loses the excess; a
client that stops reading is disconnected while the others keep receiving; a
client without the pn subprotocol is closed; and the same port serves the
manifest and the web files over HTTP, from the built-in files and from a
directory.
"""
import asyncio
import os
import socket
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.request

try:
    import websockets
except ImportError:
    print("websockets is not installed; skipping")
    sys.exit(77)

SERVER, WWW_DIR = sys.argv[1], sys.argv[2]
MANIFEST = b"# pn manifest v1 test fnv1a32=811c9dc5\n"
MSG_MAX = 1024        # the hub's size limit
RING_HOLDS = 255      # messages the hub queues at most
SUBPROTOCOL = ["pn"]


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def start(port, www_dir=None):
    args = [SERVER, str(port)] + ([www_dir] if www_dir else [])
    proc = subprocess.Popen(args)
    deadline = time.time() + 5
    while time.time() < deadline:
        if proc.poll() is not None:
            raise SystemExit(f"server exited early with {proc.returncode}")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return proc
        except OSError:
            time.sleep(0.05)
    proc.kill()
    raise SystemExit("server did not start")


def http_get(port, path):
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", timeout=5) as r:
            return r.status, r.headers.get("Content-Type", ""), r.read()
    except urllib.error.HTTPError as e:
        return e.code, "", b""


def burst(n):
    """the test server's burst command"""
    return bytes([0xFF, n & 0xFF, n >> 8, 0])


def burst_index(msg):
    return msg[0] | msg[1] << 8


def client_frame(payload):
    """One masked binary websocket frame, as a client must send it."""
    mask = os.urandom(4)
    head = bytes([0x82])
    if len(payload) < 126:
        head += bytes([0x80 | len(payload)])
    else:
        head += bytes([0x80 | 126]) + struct.pack(">H", len(payload))
    return head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload))


async def recv(ws, timeout=5):
    return await asyncio.wait_for(ws.recv(), timeout)


async def recv_until(ws, marker, timeout=5):
    """messages received before the marker"""
    got = []
    while True:
        m = await recv(ws, timeout)
        if m == marker:
            return got
        got.append(m)


async def ws_checks(port):
    uri = f"ws://127.0.0.1:{port}"
    async with websockets.connect(uri, subprotocols=SUBPROTOCOL) as a, \
               websockets.connect(uri, subprotocols=SUBPROTOCOL) as b:
        # a message goes to every client, the sender included
        await a.send(b"\x01\x02\x03\x00")
        assert await recv(a) == b"\x01\x02\x03\x00"
        assert await recv(b) == b"\x01\x02\x03\x00"

        # fragments are reassembled into one message
        await a.send([b"\x01\x02", b"\x03", b"\x04\x00"])
        assert await recv(a) == b"\x01\x02\x03\x04\x00"
        assert await recv(b) == b"\x01\x02\x03\x04\x00"

        # the size limit is inclusive
        largest = bytes(MSG_MAX - 1) + b"\x00"
        await a.send(largest)
        assert await recv(a) == largest
        assert await recv(b) == largest

        # a message without the delimiter, a text message, and one over the
        # limit are dropped
        await a.send(b"\x05\x06")
        await a.send("text")
        await a.send(bytes(MSG_MAX) + b"\x00")
        await b.send(b"\x07\x00")
        assert await recv(a) == b"\x07\x00"
        assert await recv(b) == b"\x07\x00"

        # a burst arrives complete and in order at both
        msgs = [bytes([i % 255 + 1, 0]) for i in range(150)]
        for m in msgs:
            await a.send(m)
        for c in (a, b):
            assert [await recv(c) for _ in msgs] == msgs, "burst order or completeness"

        # a single request producing more replies than the hub queues loses
        # the excess, and the hub carries on afterwards
        await a.send(burst(300))
        first = await recv(a)
        await a.send(b"\x09\x00")
        got = [first] + await recv_until(a, b"\x09\x00")
        assert [burst_index(m) for m in got] == list(range(RING_HOLDS)), len(got)
        got_b = await recv_until(b, b"\x09\x00")
        assert [burst_index(m) for m in got_b] == list(range(RING_HOLDS)), len(got_b)

        # requests that would overfill the queue pause the client instead:
        # three burst commands in one TCP write reach the hub in one read
        a.transport.write(b"".join(client_frame(burst(100)) for _ in range(3)))
        first = await recv(a)
        await a.send(b"\x0a\x00")
        got = [first] + await recv_until(a, b"\x0a\x00", timeout=10)
        assert [burst_index(m) for m in got] == list(range(100)) * 3, len(got)
        got_b = await recv_until(b, b"\x0a\x00", timeout=10)
        assert len(got_b) == 300, len(got_b)

    # a client connecting after a message was queued does not receive it, and
    # after every client left a new one still receives
    async with websockets.connect(uri, subprotocols=SUBPROTOCOL) as c:
        await c.send(b"\x0b\x00")
        assert await recv(c) == b"\x0b\x00"
        async with websockets.connect(uri, subprotocols=SUBPROTOCOL) as d:
            await c.send(b"\x0c\x00")
            assert await recv(c) == b"\x0c\x00"
            assert await recv(d) == b"\x0c\x00"

    # a client that stops reading is disconnected; the others keep receiving
    slow_sock = socket.socket()
    slow_sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2048)
    slow_sock.connect(("127.0.0.1", port))
    async with websockets.connect(uri, sock=slow_sock, subprotocols=SUBPROTOCOL) as slow, \
               websockets.connect(uri, subprotocols=SUBPROTOCOL) as fast:
        total = 0
        for _ in range(30):
            await fast.send(burst(200))
            for _ in range(200):
                m = await recv(fast, 10)
                assert burst_index(m) == total % 200 and len(m) == MSG_MAX
                total += 1
        assert total == 6000
        try:
            while True:
                await recv(slow, 10)
        except websockets.exceptions.ConnectionClosed:
            pass
        assert slow.close_code == 1006, slow.close_code

    # a client that does not request the subprotocol is closed by the hub
    async with websockets.connect(uri) as e:
        try:
            await recv(e, 5)
        except websockets.exceptions.ConnectionClosed:
            pass
        else:
            raise AssertionError("a client without the pn subprotocol was kept open")


def http_checks(port, expect_index):
    status, ctype, body = http_get(port, "/s9.manifest")
    assert (status, body) == (200, MANIFEST), (status, body)
    assert ctype.startswith("text/plain"), ctype
    status, ctype, body = http_get(port, "/")
    assert status == 200 and body == expect_index, (status, len(body))
    assert ctype.startswith("text/html"), ctype
    status, _, body = http_get(port, "/index.html")
    assert status == 200 and body == expect_index
    for path in ("/missing.html", "/s9.manifest/index.html", "/s9.manifest/"):
        status, _, _ = http_get(port, path)
        assert status == 404, (path, status)


def main():
    with open(f"{WWW_DIR}/index.html", "rb") as f:
        index = f.read()

    for www_dir in (None, WWW_DIR):
        port = free_port()
        proc = start(port, www_dir)
        try:
            asyncio.run(ws_checks(port))
            http_checks(port, index)
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
        print(f"ok: {'built-in files' if www_dir is None else 'directory'}")


main()
