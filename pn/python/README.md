# pn — Python

Python implementation of the pn parameter node library. Protocol-compatible with the C implementation (COBS framing, CRC-16, protobuf wire format).

## Install

```bash
# From the pn/ directory: the package, its test extras, and the code generators
./make_venv.sh
source venv/bin/activate
```

## Server

Define a parameter tree using descriptors, then serve it:

```python
import pn
from pn import transport

class Channel:
    gain = pn.Float(default=0.0, min=-72.0, max=6.0, step=0.1)
    mute = pn.Bool(default=False)
    input = pn.Enum(labels=["mic", "line"], default=0)
    name = pn.String(default="", max_len=32)

    @pn.Action
    def reset(self):
        self.gain = 0.0
        self.mute = False

class Mixer:
    master = pn.Float(default=0.0, min=-72.0, max=6.0, step=0.1)
    ch1 = pn.Group(Channel)
    ch2 = pn.Group(Channel)

server = pn.Server(Mixer(), name="mixer")

# async: over websockets (a hub: every client's packets feed the server, and
# everything it sends goes to every client), or over a Unix socket
await transport.serve_ws(server, "0.0.0.0", 8080)
await transport.serve_uds(server, "/tmp/pn.sock")
```

### Descriptors

| Descriptor | Kwargs | Notes |
|---|---|---|
| `pn.Float` | `default`, `min`, `max`, `step` | Clamped to [min, max] |
| `pn.Int` | `default`, `min`, `max`, `step` | Clamped to [min, max] |
| `pn.Bool` | `default` | |
| `pn.Enum` | `labels`, `default` | `default` is an index; clamped to [0, len-1] |
| `pn.String` | `default`, `max_len` | Truncated to `max_len` |
| `pn.Group(cls)` | | Nests a child group; `cls` is a class with its own descriptors |
| `@pn.Action` | | Decorates a method; triggered by clients via Set with no value |

### Auto-notify

Setting a parameter on the server side automatically sends an Update to
connected clients:

```python
@pn.Action
def reset(self):
    self.gain = 0.0   # clients are notified automatically
    self.mute = False  # each set triggers its own Update
```

This works for any `__set__` on a bound tree — whether triggered by a client
message, an action, or server-side logic. Before the tree is bound to a server
(or without a server at all), descriptors work normally with no notification.

### Callbacks

Register a callback when a parameter is set (by a client or server-side code):

```python
class Channel:
    gain = pn.Float(default=0.0, min=-72.0, max=6.0)

    @gain.on_set
    def _on_gain(self, value):
        print(f"gain changed to {value}")
```

## Client

```python
import pn
from pn import transport

client = pn.Client()

@client.on_update
def on_update(path, value, ptype):
    print(f"{path} = {value}")

read_task, close = await transport.connect_ws(client, "ws://localhost:8080")   # or connect_uds(client, path)

# Typed setters
client.set_float("mixer/ch1/gain", -6.0)
client.set_bool("mixer/ch1/mute", True)
client.set_int("mixer/ch1/input", 1)
client.set_enum("mixer/ch1/input", 1)
client.set_string("mixer/ch1/name", "vocals")

# Auto-typed (infers from Python type)
client.set("mixer/ch1/gain", -6.0)

# Get (response arrives via on_update callback)
client.get("mixer/ch1/gain")     # single param
client.get("mixer/ch1")          # group — returns all leaves

# Action
client.action("mixer/ch1/reset")

close()
```

`connect_ws_persistent` and `connect_uds_persistent` keep reconnecting instead, calling `on_connect` and `on_disconnect` as the connection comes and goes, and drop sends while disconnected. A websocket carries one pn packet per binary message under the subprotocol `pn`.

## Schema manifest

A server's **schema** is the shape of its tree: every parameter's name, type, and constraints. `pn.manifest` renders it as a **manifest**, the canonical text form specified in [MANIFEST.md](../MANIFEST.md), and computes the **schema hash**, a fingerprint of the manifest. The C and Python implementations produce byte-identical manifests for the same schema.

```python
from pn import manifest

text = manifest.dump(Mixer(), "mixer")          # header line and one entry per leaf
m = manifest.parse(open("mixer.manifest", "rb").read())
m.root, m.declared_hash, m.schema_hash          # root name, hash from the header, hash of the body
m.by_path["mixer/ch1/gain"].min                 # constraints per entry
m.entries[0].readonly                           # True for a node the server reports but never sets;
                                                # declare one with readonly=True on the descriptor
manifest.schema_hash(text)                      # hash of any manifest text
```

A server validates its tree when it is created and raises `SchemaError` if the tree has no manifest: names and enum labels must follow the name rules in MANIFEST.md, an enum needs at least one label, int constraints must fit an int32, and paths must fit the wire. The schema is fixed at creation; `server.manifest` is the text and `server.schema_hash` the hash.

Over the wire, a server answers two requests. `client.get_schema(root)` asks for the hash, delivered to `on_schema(hash, root)`; an empty root asks every server on the transport, and each replies with its own root. `client.get_manifest(root)` asks for the manifest, delivered complete to `on_manifest(data)`. The coroutines `request_schema()` and `request_manifest()` return the reply instead, and `check_schema()` compares a manifest with what its server reports:

```python
m = manifest.parse(open("mixer.manifest", "rb").read())
if not await client.check_schema(m):    # warns by default; on_mismatch="raise" or "ignore"
    ...
```

How a client responds to a mismatch is its own decision. The server does not know or care what a client believes the schema to be: it enforces its own constraints and ignores paths it does not have. A client that only touches parameters it knows usually keeps working after parameters are added, so the default is a `SchemaMismatchWarning` rather than an error.

### pn-manifest

The `pn-manifest` command works with manifests from the shell:

```bash
pn-manifest pull ws://localhost:8080 > mixer.manifest   # the manifest of a running server
pn-manifest hash mixer.manifest                         # the schema hash of a file
pn-manifest check mixer.manifest ws://localhost:8080    # compare a file with a running server
```

The server is a `ws://` URL or the path of a Unix socket.

## Wire protocol

Matches the C implementation exactly:

1. Protobuf serialize (betterproto, compatible with nanopb)
2. CRC-16-CCITT append (little-endian)
3. COBS encode
4. `0x00` frame delimiter

### Type coercion

When a client sends a value whose type doesn't match the parameter, the server
coerces if possible:

| From | To | Rule |
|---|---|---|
| int | float | `float(v)` |
| float | int | `int(v)` (truncate) |
| int | bool | `v != 0` |
| int | enum | identity |

All other mismatches are silently rejected.

## Tests

```bash
source venv/bin/activate
python -m pytest python/tests/ -v
```
