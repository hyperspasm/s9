"""End-to-end tests — ported from test_pn.c"""
import pn
from pn._types import ParamType


# --- Test tree (same as C) ---

class Sub:
    level = pn.Int(default=50, min=0, max=100, step=1)
    pan = pn.Float(default=0.0, min=-1.0, max=1.0, step=0.01)


class Root:
    gain = pn.Float(default=0.0, min=-72.0, max=6.0, step=0.1)
    mute = pn.Bool(default=False)
    input = pn.Enum(labels=["mic", "line"], default=0)
    name = pn.String(default="", max_len=32)
    sub = pn.Group(Sub)

    @pn.Action
    def reset(self):
        pass


# --- E2E helpers ---

class E2E:
    def __init__(self, root=None, name="root", loopback=False):
        """Wire a client to a server over in-memory buffers.

        With loopback, bytes are delivered immediately in both directions
        instead of waiting for roundtrip(), so coroutines that await a reply
        resolve as soon as they send.
        """
        self.root = root if root is not None else Root()
        self.updates = []
        self.schemas = []
        self.manifests = []

        self.server = pn.Server(self.root, name=name)
        self.client = pn.Client(on_update=self._on_update,
                                on_schema=self._on_schema,
                                on_manifest=self._on_manifest)

        # Wire them together: client sends → server receives, server sends → client receives
        self.srv_tx = bytearray()
        self.cli_tx = bytearray()

        if loopback:
            self.server.bind(lambda data: self.client.feed(data))
            self.client.bind(lambda data: self.server.feed(data))
        else:
            self.server.bind(lambda data: self.srv_tx.extend(data))
            self.client.bind(lambda data: self.cli_tx.extend(data))

    def _on_update(self, path, value, ptype):
        self.updates.append({"path": path, "value": value, "type": ptype})

    def _on_schema(self, hash_, root):
        self.schemas.append((hash_, root))

    def _on_manifest(self, data):
        self.manifests.append(data)

    def roundtrip(self):
        """Route client output to server, then server output to client."""
        self.server.feed(bytes(self.cli_tx))
        self.cli_tx.clear()
        self.client.feed(bytes(self.srv_tx))
        self.srv_tx.clear()


# --- Tests ---

def test_set_float():
    e = E2E()
    e.client.set_float("root/gain", 3.0)
    e.roundtrip()
    assert len(e.updates) == 1
    assert e.updates[0]["path"] == "root/gain"
    assert e.updates[0]["type"] == ParamType.FLOAT
    assert e.updates[0]["value"] == 3.0
    assert e.root.gain == 3.0


def test_set_clamp():
    e = E2E()
    e.client.set_float("root/gain", 100.0)
    e.roundtrip()
    assert len(e.updates) == 1
    assert e.updates[0]["value"] == 6.0  # clamped to max
    assert e.root.gain == 6.0


def test_get_float():
    e = E2E()
    e.root.gain = -6.0
    e.srv_tx.clear()  # drain auto-notify from direct set
    e.client.get("root/gain")
    e.roundtrip()
    assert len(e.updates) == 1
    assert e.updates[0]["path"] == "root/gain"
    assert e.updates[0]["type"] == ParamType.FLOAT
    assert e.updates[0]["value"] == -6.0


def test_get_group():
    e = E2E()
    e.root.sub.level = 75
    e.root.sub.pan = 0.5
    e.srv_tx.clear()
    e.client.get("root/sub")
    e.roundtrip()
    assert len(e.updates) == 2
    assert e.updates[0]["path"] == "root/sub/level"
    assert e.updates[0]["type"] == ParamType.INT
    assert e.updates[0]["value"] == 75
    assert e.updates[1]["path"] == "root/sub/pan"
    assert e.updates[1]["type"] == ParamType.FLOAT
    assert e.updates[1]["value"] == 0.5


def test_get_group_trailing_slash():
    e = E2E()
    e.root.sub.level = 42
    e.root.sub.pan = -0.5
    e.srv_tx.clear()
    e.client.get("root/sub/")
    e.roundtrip()
    assert len(e.updates) == 2
    # Paths must not have double slashes
    assert e.updates[0]["path"] == "root/sub/level"
    assert e.updates[1]["path"] == "root/sub/pan"


def test_action():
    e = E2E()
    fired = [False]
    original_reset = Root.reset._fn

    def track_reset(self):
        fired[0] = True
        original_reset(self)

    Root.reset._fn = track_reset
    try:
        e.client.action("root/reset")
        e.server.feed(bytes(e.cli_tx))
        assert fired[0]
        # No update sent for actions
        assert len(e.srv_tx) == 0
    finally:
        Root.reset._fn = original_reset


def test_coerce_int_to_float():
    e = E2E()
    e.client.set_int("root/gain", 3)
    e.roundtrip()
    assert len(e.updates) == 1
    assert e.updates[0]["type"] == ParamType.FLOAT
    assert e.updates[0]["value"] == 3.0
    assert e.root.gain == 3.0


def test_wrong_root():
    e = E2E()
    e.client.set_float("other/gain", 3.0)
    e.roundtrip()
    assert len(e.updates) == 0
    assert e.root.gain == 0.0


def test_set_bool():
    e = E2E()
    e.client.set_bool("root/mute", True)
    e.roundtrip()
    assert len(e.updates) == 1
    assert e.updates[0]["path"] == "root/mute"
    assert e.updates[0]["type"] == ParamType.BOOL
    assert e.updates[0]["value"] is True
    assert e.root.mute is True


def test_set_int():
    e = E2E()
    e.client.set_int("root/sub/level", 75)
    e.roundtrip()
    assert len(e.updates) == 1
    assert e.updates[0]["path"] == "root/sub/level"
    assert e.updates[0]["type"] == ParamType.INT
    assert e.updates[0]["value"] == 75
    assert e.root.sub.level == 75


def test_set_enum():
    e = E2E()
    e.client.set_enum("root/input", 1)
    e.roundtrip()
    assert len(e.updates) == 1
    assert e.updates[0]["path"] == "root/input"
    assert e.updates[0]["type"] == ParamType.ENUM
    assert e.updates[0]["value"] == 1
    assert e.root.input == 1


def test_set_string():
    e = E2E()
    e.client.set_string("root/name", "ch1")
    e.roundtrip()
    assert len(e.updates) == 1
    assert e.updates[0]["path"] == "root/name"
    assert e.updates[0]["type"] == ParamType.STRING
    assert e.updates[0]["value"] == "ch1"
    assert e.root.name == "ch1"


def test_get_root():
    e = E2E()
    e.root.gain = -6.0
    e.root.mute = True
    e.root.input = 1
    e.root.name = "master"
    e.root.sub.level = 75
    e.root.sub.pan = 0.5
    e.srv_tx.clear()
    e.client.get("root")
    e.roundtrip()
    # Should get all leaf params (gain, mute, input, name, level, pan)
    # but NOT reset (action) or sub (group)
    assert len(e.updates) == 6
    assert e.updates[0]["path"] == "root/gain"
    assert e.updates[0]["type"] == ParamType.FLOAT
    assert e.updates[0]["value"] == -6.0
    assert e.updates[1]["path"] == "root/mute"
    assert e.updates[1]["type"] == ParamType.BOOL
    assert e.updates[1]["value"] is True
    assert e.updates[2]["path"] == "root/input"
    assert e.updates[2]["type"] == ParamType.ENUM
    assert e.updates[2]["value"] == 1
    assert e.updates[3]["path"] == "root/name"
    assert e.updates[3]["type"] == ParamType.STRING
    assert e.updates[3]["value"] == "master"
    assert e.updates[4]["path"] == "root/sub/level"
    assert e.updates[4]["type"] == ParamType.INT
    assert e.updates[4]["value"] == 75
    assert e.updates[5]["path"] == "root/sub/pan"
    assert e.updates[5]["type"] == ParamType.FLOAT
    assert e.updates[5]["value"] == 0.5


def test_coerce_float_to_int():
    e = E2E()
    e.client.set_float("root/sub/level", 42.7)
    e.roundtrip()
    assert len(e.updates) == 1
    assert e.updates[0]["type"] == ParamType.INT
    assert e.updates[0]["value"] == 42
    assert e.root.sub.level == 42


def test_coerce_int_to_bool():
    e = E2E()
    e.client.set_int("root/mute", 1)
    e.roundtrip()
    assert len(e.updates) == 1
    assert e.updates[0]["type"] == ParamType.BOOL
    assert e.updates[0]["value"] is True
    assert e.root.mute is True


def test_coerce_int_to_enum():
    e = E2E()
    e.client.set_int("root/input", 1)
    e.roundtrip()
    assert len(e.updates) == 1
    assert e.updates[0]["type"] == ParamType.ENUM
    assert e.updates[0]["value"] == 1
    assert e.root.input == 1


def test_unsupported_coercion():
    e = E2E()
    # String -> float: no coercion rule, silently rejected
    e.client.set_string("root/gain", "hello")
    e.roundtrip()
    assert len(e.updates) == 0
    assert e.root.gain == 0.0


def test_sequential_ops():
    e = E2E()
    # First: set gain
    e.client.set_float("root/gain", 3.0)
    e.roundtrip()
    assert len(e.updates) == 1
    assert e.root.gain == 3.0

    # Second: set mute
    e.client.set_bool("root/mute", True)
    e.roundtrip()
    assert len(e.updates) == 2
    assert e.root.mute is True

    # Third: get gain (confirms first set persisted)
    e.client.get("root/gain")
    e.roundtrip()
    assert len(e.updates) == 3
    assert e.updates[2]["value"] == 3.0


def test_auto_notify():
    e = E2E()
    # Setting a value on the server side auto-notifies clients
    e.root.gain = -12.0
    e.client.feed(bytes(e.srv_tx))
    e.srv_tx.clear()
    assert len(e.updates) == 1
    assert e.updates[0]["path"] == "root/gain"
    assert e.updates[0]["type"] == ParamType.FLOAT
    assert e.updates[0]["value"] == -12.0


def test_auto_notify_nested():
    e = E2E()
    e.root.sub.level = 42
    e.client.feed(bytes(e.srv_tx))
    e.srv_tx.clear()
    assert len(e.updates) == 1
    assert e.updates[0]["path"] == "root/sub/level"
    assert e.updates[0]["type"] == ParamType.INT
    assert e.updates[0]["value"] == 42


def test_auto_notify_no_server():
    """Descriptors work fine without a server — no auto-notify."""
    class Standalone:
        val = pn.Float(default=0.0, min=0.0, max=10.0)

    obj = Standalone()
    obj.val = 5.0
    assert obj.val == 5.0  # no crash, no server needed


def test_auto_notify_from_action():
    """Action that sets params triggers auto-notify for each param."""
    class MyRoot:
        gain = pn.Float(default=0.0, min=-72.0, max=6.0)

        @pn.Action
        def reset(self):
            self.gain = 0.0

    root = MyRoot()
    root.gain = -12.0
    server = pn.Server(root, name="root")
    client = pn.Client()
    updates = []
    client.on_update(lambda p, v, t: updates.append({"path": p, "value": v, "type": t}))
    srv_tx = bytearray()
    cli_tx = bytearray()
    server.bind(lambda data: srv_tx.extend(data))
    client.bind(lambda data: cli_tx.extend(data))

    client.action("root/reset")
    server.feed(bytes(cli_tx)); cli_tx.clear()
    client.feed(bytes(srv_tx)); srv_tx.clear()
    assert len(updates) == 1
    assert updates[0]["path"] == "root/gain"
    assert updates[0]["value"] == 0.0
    assert root.gain == 0.0


# --- Deep groups ---

class Inner:
    val = pn.Int(default=0, min=0, max=100)


class Middle:
    inner = pn.Group(Inner)
    flag = pn.Bool(default=False)


class Deep:
    middle = pn.Group(Middle)
    top_val = pn.Float(default=0.0, min=0.0, max=1.0)


def test_deep_set_and_get():
    """Set and get a param 3 groups deep."""
    root = Deep()
    server = pn.Server(root, name="deep")
    client = pn.Client()
    updates = []
    client.on_update(lambda p, v, t: updates.append({"path": p, "value": v, "type": t}))
    srv_tx = bytearray()
    cli_tx = bytearray()
    server.bind(lambda data: srv_tx.extend(data))
    client.bind(lambda data: cli_tx.extend(data))

    client.set_int("deep/middle/inner/val", 42)
    server.feed(bytes(cli_tx)); cli_tx.clear()
    client.feed(bytes(srv_tx)); srv_tx.clear()
    assert len(updates) == 1
    assert updates[0]["path"] == "deep/middle/inner/val"
    assert updates[0]["value"] == 42
    assert root.middle.inner.val == 42


def test_deep_group_get():
    """Get on a group 3 levels deep returns all leaves in subtree."""
    root = Deep()
    root.middle.inner.val = 77
    root.middle.flag = True
    server = pn.Server(root, name="deep")
    client = pn.Client()
    updates = []
    client.on_update(lambda p, v, t: updates.append({"path": p, "value": v, "type": t}))
    srv_tx = bytearray()
    cli_tx = bytearray()
    server.bind(lambda data: srv_tx.extend(data))
    client.bind(lambda data: cli_tx.extend(data))

    client.get("deep/middle")
    server.feed(bytes(cli_tx)); cli_tx.clear()
    client.feed(bytes(srv_tx)); srv_tx.clear()
    assert len(updates) == 2
    assert updates[0]["path"] == "deep/middle/inner/val"
    assert updates[0]["value"] == 77
    assert updates[1]["path"] == "deep/middle/flag"
    assert updates[1]["value"] is True


def test_deep_root_get():
    """Get on root returns all leaves including deeply nested."""
    root = Deep()
    root.top_val = 0.5
    root.middle.flag = True
    root.middle.inner.val = 99
    server = pn.Server(root, name="deep")
    client = pn.Client()
    updates = []
    client.on_update(lambda p, v, t: updates.append({"path": p, "value": v, "type": t}))
    srv_tx = bytearray()
    cli_tx = bytearray()
    server.bind(lambda data: srv_tx.extend(data))
    client.bind(lambda data: cli_tx.extend(data))

    client.get("deep")
    server.feed(bytes(cli_tx)); cli_tx.clear()
    client.feed(bytes(srv_tx)); srv_tx.clear()
    # top_val, then middle/inner/val, middle/flag
    assert len(updates) == 3
    paths = [u["path"] for u in updates]
    assert "deep/top_val" in paths
    assert "deep/middle/inner/val" in paths
    assert "deep/middle/flag" in paths


# --- Action with value (should still fire) ---

def test_action_with_value():
    """Sending a Set to an action path (even with a value) triggers the action."""
    e = E2E()
    fired = [False]
    original_reset = Root.reset._fn

    def track_reset(self):
        fired[0] = True
        original_reset(self)

    Root.reset._fn = track_reset
    try:
        # Send a Set with a value to an action path
        from pn import _proto as pb, _codec
        msg = pb.Message(set=pb.Set(path="root/reset", value=pb.Value(int_val=1)))
        e.server.feed(_codec.encode(msg))
        assert fired[0]
    finally:
        Root.reset._fn = original_reset


# --- Bad paths ---

def test_set_nonexistent_path():
    """Setting a non-existent path is silently ignored."""
    e = E2E()
    e.client.set_float("root/nonexistent", 1.0)
    e.roundtrip()
    assert len(e.updates) == 0


def test_get_nonexistent_path():
    """Getting a non-existent path returns no updates."""
    e = E2E()
    e.client.get("root/nonexistent")
    e.roundtrip()
    assert len(e.updates) == 0


def test_set_path_through_leaf():
    """Trying to descend through a leaf param returns nothing."""
    e = E2E()
    e.client.set_float("root/gain/sub", 1.0)
    e.roundtrip()
    assert len(e.updates) == 0


# --- Schema and manifest over the wire ---

import asyncio
import re
import warnings

import pytest

from pn import _proto as pb


def test_schema_get():
    """A schema request addressed to the server's root is answered with its hash and root."""
    e = E2E()
    e.client.get_schema("root")
    e.roundtrip()
    assert e.schemas == [(e.server.schema_hash, "root")]


def test_schema_get_any():
    """A schema request with no root is answered by every server."""
    e = E2E()
    e.client.get_schema()
    e.client.get_schema("")
    e.roundtrip()
    assert e.schemas == [(e.server.schema_hash, "root")] * 2


def test_schema_get_other_root():
    """A schema request addressed to another root is ignored."""
    e = E2E()
    e.client.get_schema("other")
    e.roundtrip()
    assert e.schemas == []


def test_manifest_get():
    """A manifest request is answered with the server's manifest, byte for byte."""
    e = E2E()
    e.client.get_manifest("root")
    e.roundtrip()
    assert e.manifests == [e.server.manifest.encode("utf-8")]


def test_manifest_get_other_root():
    """A manifest request addressed to another root is ignored."""
    e = E2E()
    e.client.get_manifest("other")
    e.roundtrip()
    assert e.manifests == []


# Enough leaves for a manifest longer than two chunks
Big = type("Big", (), {f"p{i:02d}": pn.Float(min=-1.0, max=1.0, step=0.0) for i in range(24)})


def test_manifest_chunked():
    """A long manifest is streamed in full chunks with last set only on the final one,
    and the client reassembles it."""
    e = E2E(Big(), name="big")
    data = e.server.manifest.encode("utf-8")
    assert len(data) > 2 * 512

    responses = e.server._dispatch_message(pb.Message(manifest_get=pb.ManifestGet(root="big")))
    chunks = [r.manifest_chunk for r in responses]
    assert [len(c.data) for c in chunks] == [512, 512, len(data) - 1024]
    assert [c.last for c in chunks] == [False, False, True]

    e.client.get_manifest("big")
    e.roundtrip()
    assert e.manifests == [data]


async def test_request_schema():
    e = E2E(loopback=True)
    assert await e.client.request_schema("root") == (e.server.schema_hash, "root")


async def test_request_schema_times_out():
    e = E2E(loopback=True)
    with pytest.raises(asyncio.TimeoutError):
        await e.client.request_schema("other", timeout=0.05)


async def test_request_manifest():
    e = E2E(loopback=True)
    assert await e.client.request_manifest("root") == e.server.manifest.encode("utf-8")


def _stale(text):
    """The manifest with a wrong declared hash, as after a schema change."""
    return re.sub(r"fnv1a32=[0-9a-f]{8}", "fnv1a32=00000000", text)


async def test_check_schema_match():
    e = E2E(loopback=True)
    assert await e.client.check_schema(e.server.manifest) is True
    assert await e.client.check_schema(pn.manifest.parse(e.server.manifest)) is True


async def test_check_schema_without_header_uses_body_hash():
    e = E2E(loopback=True)
    body = e.server.manifest.split("\n", 1)[1]
    assert await e.client.check_schema(body) is True


async def test_check_schema_mismatch_warns():
    e = E2E(loopback=True)
    with pytest.warns(pn.SchemaMismatchWarning):
        assert await e.client.check_schema(_stale(e.server.manifest)) is False


async def test_check_schema_mismatch_raises():
    e = E2E(loopback=True)
    with pytest.raises(pn.SchemaMismatch):
        await e.client.check_schema(_stale(e.server.manifest), on_mismatch="raise")


async def test_check_schema_mismatch_ignore():
    e = E2E(loopback=True)
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        assert await e.client.check_schema(_stale(e.server.manifest), on_mismatch="ignore") is False
