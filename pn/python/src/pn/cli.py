"""pn-manifest: work with schema manifests from the command line.

    pn-manifest pull SOCKET [--root ROOT]   print the manifest of a running server
    pn-manifest hash FILE                   print the schema hash of a manifest file
    pn-manifest check FILE SOCKET           compare a manifest file with a running server
"""
import argparse
import asyncio
import sys

from . import manifest, transport
from .client import Client


async def _connect(socket_path):
    client = Client()
    try:
        _, close = await transport.connect_uds(client, socket_path)
    except (ConnectionRefusedError, FileNotFoundError) as e:
        sys.exit(f"cannot connect to {socket_path}: {e}")
    return client, close


async def _pull(args):
    client, close = await _connect(args.socket)
    try:
        root = args.root
        if not root:
            # An empty root is answered by every server; take the first
            _, root = await client.request_schema("", timeout=args.timeout)
        data = await client.request_manifest(root, timeout=args.timeout)
    except asyncio.TimeoutError:
        sys.exit("no reply from the server")
    finally:
        close()
    sys.stdout.buffer.write(data)
    sys.stdout.buffer.flush()


async def _check(args):
    with open(args.file, "rb") as f:
        m = manifest.parse(f.read())
    expected = m.declared_hash if m.declared_hash is not None else m.schema_hash

    client, close = await _connect(args.socket)
    try:
        reported, _ = await client.request_schema(m.root, timeout=args.timeout)
    except asyncio.TimeoutError:
        sys.exit(f"no reply from a server named {m.root!r}")
    finally:
        close()

    if reported == expected:
        print(f"{m.root}: schema hash {reported:08x} matches")
        return 0
    print(f"{m.root}: server reports {reported:08x}, {args.file} declares {expected:08x}")
    return 1


def main(argv=None):
    parser = argparse.ArgumentParser(prog="pn-manifest", description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)

    pull = sub.add_parser("pull", help="print the manifest of a running server")
    pull.add_argument("socket", help="path of the server's Unix socket")
    pull.add_argument("--root", help="root name of the server (default: the first server that answers)")
    pull.add_argument("--timeout", type=float, default=5.0)

    hash_ = sub.add_parser("hash", help="print the schema hash of a manifest file")
    hash_.add_argument("file")

    check = sub.add_parser("check", help="compare a manifest file with a running server")
    check.add_argument("file")
    check.add_argument("socket", help="path of the server's Unix socket")
    check.add_argument("--timeout", type=float, default=2.0)

    args = parser.parse_args(argv)
    if args.command == "pull":
        return asyncio.run(_pull(args))
    if args.command == "hash":
        with open(args.file, "rb") as f:
            print(f"{manifest.schema_hash(f.read()):08x}")
        return 0
    return asyncio.run(_check(args))


if __name__ == "__main__":
    sys.exit(main())
