# pn — Parameter Node Library

A lightweight C library for exposing application parameters over a bidirectional byte stream. Designed to be transport-agnostic (TCP, UART, unix socket, stdio) and suitable for embedded targets.

## Overview

`pn` provides a hierarchical tree of typed parameter nodes. Each node has a name, a type, and getter/setter function pointers that bind to application state. A remote client connects over a byte stream and can read and write values by path.

The library has three components:

- **`pn_node`** — the node type (`pn_node_t`). Defines value types, type metadata, and the node structure (name, type, meta).
- **`pn_server`** — the server. Defines `pn_param_t` (the node with getter/setter callbacks), owns the parameter tree, and dispatches incoming requests.
- **`pn_client`** — the client. Sends Set/Get messages and receives Update messages via a callback.

The server host provides:

- The parameter tree, assembled from its modules using `pn_param_t`
- The getter/setter implementations
- A `send` callback for outbound bytes (any byte stream: TCP, UART, unix socket, stdio)

The client host provides:

- A `send` callback for outbound bytes
- An `on_update` callback to receive parameter updates from the server

## Architecture

```
                    pn_server_feed()
                         │
pn_param_t tree ─── pn_server ──→ send()
                         │
                    pn_param_notify()
```

Inbound: the server host receives bytes and calls `pn_server_feed()`. The server decodes and dispatches to the parameter tree.

Outbound: the server encodes a response and calls the host's `send` callback. The host can also push updates directly via `pn_param_notify()`.

## Types

`pn_node_t` is the base node type. It holds the parameter's identity and constraints:

```c
typedef struct pn_node {
    const char *name;
    pn_type_t type;       // PN_NONE, PN_ACTION, PN_BOOL, PN_INT, PN_FLOAT, PN_ENUM, PN_STRING
    pn_meta_t meta;       // type-specific metadata (min/max/step, enum labels, etc.)
} pn_node_t;
```

`pn_param_t` wraps `pn_node_t` with getter/setter callbacks and tree linkage:

```c
typedef struct pn_param {
    pn_node_t node;             // name, type, meta (must be first member)
    struct pn_param *children;  // groups only
    uint32_t num_children;      // groups only
    void *ctx;                  // passed to set/get callbacks
    pn_set_fn_t set;            // setter (type-specific, see pn_set_fn_t)
    pn_get_fn_t get;            // getter (type-specific, see pn_get_fn_t)
    struct {
        struct pn_server *server;
        struct pn_param *parent;
    } bind;                     // populated by pn_server_start()
} pn_param_t;
```

## Protocol

The protocol is stateless. Clients are expected to know the parameter schema. Parameters are addressed by full path from the root (e.g. `"mixer/ch1/gain"`). The first segment must match the server's root node name — messages addressed to a different root are silently ignored, allowing multiple servers to share a transport. Paths are derived from node names in the tree and are stable across server restarts.

### Message types

- **Set** — client sends a path and value to update a parameter
- **Get** — client requests the current value of a parameter (or all leaf values under a group)
- **Update** — server sends a path and value when a parameter changes

### Type coercion

The server coerces values when the wire type doesn't match the parameter type:

- `int` → `float`, `float` → `int` (truncated)
- `int` → `bool` (nonzero = true)
- `int` → `enum`

Other mismatches (e.g. `string` → `float`) are silently rejected. Values are clamped to the parameter's declared range after coercion.

## Module Pattern

Server modules build their own parameter subtrees using `pn_param_t` arrays embedded in their structs. Inline constructors (`pn_float`, `pn_int`, `pn_bool`, `pn_enum`, `pn_string`, `pn_action`, `pn_group`) simplify tree construction.

Every setter and getter receives a `void *ctx` pointer from the param, allowing the same function to operate on different instances.

### Example: mixer with instanced channels

```c
// mixer.h
typedef struct {
    float gain;
    float pan;
    uint32_t input;
    pn_param_t nodes[3];
    pn_param_t group;
    char name[8];
} channel_t;

typedef struct {
    float master_gain;
    channel_t channels[NUM_CHANNELS];
    pn_param_t children[NUM_CHANNELS + 1];
    pn_param_t root;
} mixer_t;

void mixer_init(mixer_t *m);
pn_param_t *mixer_build_tree(mixer_t *m);

// mixer.c
static void  set_gain(void *ctx, float val) { ((channel_t *)ctx)->gain = val; }
static float get_gain(void *ctx)            { return ((channel_t *)ctx)->gain; }

pn_param_t *mixer_build_tree(mixer_t *m) {
    for (int i = 0; i < NUM_CHANNELS; i++) {
        channel_t *ch = &m->channels[i];
        uint32_t c = 0;
        ch->nodes[c++] = pn_float("gain", ch,
            set_gain, get_gain, -72.0f, 6.0f, 0.1f);
        // ... more params ...
        snprintf(ch->name, sizeof(ch->name), "ch%d", i + 1);
        ch->group = pn_group(ch->name, ch->nodes, c);
    }

    uint32_t n = 0;
    m->children[n++] = pn_float("master_gain", m, ...);
    for (int i = 0; i < NUM_CHANNELS; i++)
        m->children[n++] = m->channels[i].group;

    m->root = pn_group("mixer", m->children, n);
    return &m->root;
}
```

## Server Setup

The server uses caller-owned allocation: query the size, allocate, then initialize.

```c
pn_param_t *root = mixer_build_tree(&mixer);

pn_server_config_t config = {
    .root = root,
    .send = my_send,       // host-provided: write bytes to the stream
    .send_ctx = &my_io,
};
uint32_t size = pn_server_alloc_size(&config);
alignas(max_align_t) uint8_t mem[size];
pn_server_t *server = (pn_server_t *)mem;
pn_server_init(server, &config);
if (pn_server_start(server) != 0) {
    // the tree does not validate; pn_schema_validate() reports why
}
```

`pn_server_start` validates the tree before binding it: names and enum labels must follow the name rules in [MANIFEST.md](MANIFEST.md), sibling names must be unique, every enum needs at least one label, and the tree must fit within `PN_MAX_DEPTH` and `PN_MAX_PATH`. It returns nonzero for a tree that fails, and `pn_schema_validate` reports which rule failed and on which node.

### Notify

When the host modifies a parameter's backing value directly (outside of a Set message), it can push the new value to the client:

```c
my_state.gain = -6.0f;
pn_param_notify(&my_params.gain_param);
```

`pn_param_notify` reads the current value via the getter, builds the full path from the tree, and sends an Update message. The param must be bound (i.e. `pn_server_start` must have been called).

## Client Setup

The client uses the same caller-owned allocation pattern as the server.

```c
static void on_update(const char *path, const pn_value_t *val,
                      pn_type_t type, void *ctx) {
    if (type == PN_FLOAT)
        printf("%s = %.2f\n", path, val->f);
}

pn_client_config_t config = {
    .send = my_send,       // host-provided: write bytes to the stream
    .send_ctx = &my_io,
    .on_update = on_update,
    .on_update_ctx = &my_app,
};
uint32_t size = pn_client_alloc_size(&config);
alignas(max_align_t) uint8_t mem[size];
pn_client_t *client = (pn_client_t *)mem;
pn_client_init(client, &config);

// Send messages
pn_client_set_float(client, "mixer/ch1/gain", -6.0f);
pn_client_get(client, "mixer/ch1/pan");
pn_client_action(client, "mixer/ch1/reset");

// Feed received bytes
pn_client_feed(client, data, len);
```

## Transport

Both server and client use the same transport pattern:

- **Send** — a callback provided in the config. The library calls it when it has bytes to transmit.
- **Feed** — `pn_server_feed()` / `pn_client_feed()`. The host calls it when bytes arrive.

The host is responsible for getting bytes off the wire — however that works (blocking read, poll loop, interrupt, event callback). The library handles framing (COBS), integrity (CRC-16), and serialization (protobuf) internally.

## Wire Format

Messages are serialized with protobuf (nanopb), framed with COBS, and protected with CRC-16-CCITT. This is an internal implementation detail — the application only sees raw bytes.
