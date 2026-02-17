# pn — Python

Python implementation of the pn parameter node library. Protocol-compatible with the C implementation (COBS framing, CRC-16, protobuf wire format).

## Install

```bash
# From the pn/ directory
source venv/bin/activate
pip install -e python/
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

# async
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

read_task, close = await transport.connect_uds(client, "/tmp/pn.sock")

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
