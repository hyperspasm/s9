import asyncio
import warnings

import betterproto

from ._types import ParamType
from . import _proto as pb
from . import _codec
from . import manifest as _manifest


class SchemaMismatch(Exception):
    """A server's schema hash differs from the manifest the client holds."""


class SchemaMismatchWarning(UserWarning):
    """Issued by Client.check_schema() when on_mismatch is "warn"."""


_TYPE_MAP = {
    "bool_val": ParamType.BOOL,
    "int_val": ParamType.INT,
    "float_val": ParamType.FLOAT,
    "enum_val": ParamType.ENUM,
    "string_val": ParamType.STRING,
}


class Client:
    def __init__(self, on_update=None, on_schema=None, on_manifest=None):
        """Create a client.

        on_update:   fn(path: str, value, type: ParamType) -> None
        on_schema:   fn(hash: int, root: str) -> None, a server's reply to get_schema()
        on_manifest: fn(data: bytes) -> None, a complete manifest after get_manifest()
        """
        self._send = None
        self._on_update = on_update
        self._on_schema = on_schema
        self._on_manifest = on_manifest
        self._decoder = None
        self._manifest_buf = bytearray()
        self._schema_waiters = []
        self._manifest_waiters = []

    def bind(self, send_fn):
        """Bind a send function: send_fn(bytes) -> None"""
        self._send = send_fn
        self._decoder = _codec.Decoder(self._on_message)

    def on_update(self, fn):
        """Register or replace the update callback. Can be used as a decorator."""
        self._on_update = fn
        return fn

    def on_schema(self, fn):
        """Register or replace the schema callback. Can be used as a decorator."""
        self._on_schema = fn
        return fn

    def on_manifest(self, fn):
        """Register or replace the manifest callback. Can be used as a decorator."""
        self._on_manifest = fn
        return fn

    def feed(self, data):
        """Feed raw bytes from the server."""
        self._decoder.feed(data)

    # --- Set / Get / Action ---

    def set(self, path, value):
        """Send a Set message. Value type is inferred from Python type."""
        v = pb.Value()
        if isinstance(value, bool):
            v.bool_val = value
        elif isinstance(value, int):
            v.int_val = value
        elif isinstance(value, float):
            v.float_val = value
        elif isinstance(value, str):
            v.string_val = value
        else:
            raise TypeError(f"unsupported value type: {type(value)}")
        msg = pb.Message(set=pb.Set(path=path, value=v))
        self._send_message(msg)

    def set_float(self, path, value):
        msg = pb.Message(set=pb.Set(path=path, value=pb.Value(float_val=float(value))))
        self._send_message(msg)

    def set_int(self, path, value):
        msg = pb.Message(set=pb.Set(path=path, value=pb.Value(int_val=int(value))))
        self._send_message(msg)

    def set_bool(self, path, value):
        msg = pb.Message(set=pb.Set(path=path, value=pb.Value(bool_val=bool(value))))
        self._send_message(msg)

    def set_enum(self, path, value):
        msg = pb.Message(set=pb.Set(path=path, value=pb.Value(enum_val=int(value))))
        self._send_message(msg)

    def set_string(self, path, value):
        msg = pb.Message(set=pb.Set(path=path, value=pb.Value(string_val=str(value))))
        self._send_message(msg)

    def get(self, path):
        """Send a Get request."""
        msg = pb.Message(get=pb.Get(path=path))
        self._send_message(msg)

    def action(self, path):
        """Trigger an action (Set with no value)."""
        msg = pb.Message(set=pb.Set(path=path))
        self._send_message(msg)

    # --- Schema / Manifest ---

    def get_schema(self, root=""):
        """Ask for a schema hash; the reply arrives through on_schema.

        root names the server. An empty root asks every server on the
        transport, and each replies with its own root.
        """
        self._send_message(pb.Message(schema_get=pb.SchemaGet(root=root or "")))

    def get_manifest(self, root):
        """Ask the server with this root for its manifest; the complete text
        arrives through on_manifest once the last chunk has been received."""
        self._manifest_buf.clear()
        self._send_message(pb.Message(manifest_get=pb.ManifestGet(root=root)))

    async def request_schema(self, root="", *, timeout=2.0):
        """get_schema() as a coroutine: returns (hash, root) of the first reply.

        Raises asyncio.TimeoutError if no server answers within timeout.
        """
        future = asyncio.get_running_loop().create_future()
        self._schema_waiters.append(future)
        try:
            self.get_schema(root)
            return await asyncio.wait_for(future, timeout)
        finally:
            if future in self._schema_waiters:
                self._schema_waiters.remove(future)

    async def request_manifest(self, root, *, timeout=5.0):
        """get_manifest() as a coroutine: returns the manifest as bytes.

        Raises asyncio.TimeoutError if the manifest does not complete within timeout.
        """
        future = asyncio.get_running_loop().create_future()
        self._manifest_waiters.append(future)
        try:
            self.get_manifest(root)
            return await asyncio.wait_for(future, timeout)
        finally:
            if future in self._manifest_waiters:
                self._manifest_waiters.remove(future)

    async def check_schema(self, manifest, *, on_mismatch="warn", timeout=2.0):
        """Compare a manifest's hash with the hash its server reports.

        manifest is a Manifest or manifest text. Its root selects the server;
        its declared hash is compared, or the hash of its body when it has no
        header. Returns True when they match. On a mismatch, on_mismatch
        selects the response: "warn" issues a SchemaMismatchWarning and returns
        False, "raise" raises SchemaMismatch, and "ignore" returns False.
        """
        if not isinstance(manifest, _manifest.Manifest):
            manifest = _manifest.parse(manifest)
        expected = manifest.declared_hash
        if expected is None:
            expected = manifest.schema_hash

        reported, root = await self.request_schema(manifest.root, timeout=timeout)
        if reported == expected:
            return True

        message = (f"schema mismatch for {root}: server reports {reported:08x}, "
                   f"manifest declares {expected:08x}")
        if on_mismatch == "raise":
            raise SchemaMismatch(message)
        if on_mismatch == "warn":
            warnings.warn(message, SchemaMismatchWarning, stacklevel=2)
        return False

    # --- Internal ---

    def _send_message(self, msg):
        if self._send:
            self._send(_codec.encode(msg))

    @staticmethod
    def _resolve(waiters, result):
        for future in waiters:
            if not future.done():
                future.set_result(result)
        waiters.clear()

    def _on_message(self, msg):
        name, payload = betterproto.which_one_of(msg, "msg")
        if name == "update":
            self._deliver_update(payload)
        elif name == "schema":
            self._resolve(self._schema_waiters, (payload.hash, payload.root))
            if self._on_schema is not None:
                self._on_schema(payload.hash, payload.root)
        elif name == "manifest_chunk":
            self._manifest_buf.extend(payload.data)
            if payload.last:
                data = bytes(self._manifest_buf)
                self._manifest_buf.clear()
                self._resolve(self._manifest_waiters, data)
                if self._on_manifest is not None:
                    self._on_manifest(data)

    def _deliver_update(self, update):
        if self._on_update is None:
            return
        value_field, value = betterproto.which_one_of(update.value, "kind")
        if not value_field:
            return
        ptype = _TYPE_MAP.get(value_field)
        if ptype is not None:
            self._on_update(update.path, value, ptype)
