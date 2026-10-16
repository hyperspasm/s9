# pn — Parameter Node Library

A lightweight C library for exposing application parameters over a bidirectional byte stream. Designed to be transport-agnostic (TCP, UART, unix socket, stdio) and suitable for embedded targets. The same wire format has a [Python](python/README.md) implementation, server and client, and a [JavaScript](js/README.md) client for browsers and Node.

## Overview

`pn` provides a hierarchical tree of typed parameter nodes. Each node has a name, a type, and getter/setter function pointers that bind to application state. A remote client connects over a byte stream and can read and write values by path.

The library has four components:

- **`pn_node`** — the node type (`pn_node_t`). Defines value types, type metadata, and the node structure (name, type, meta).
- **`pn_server`** — the server. Defines `pn_param_t` (the node with getter/setter callbacks), owns the parameter tree, and dispatches incoming requests.
- **`pn_client`** — the client. Sends Set/Get messages and receives Update messages via a callback.
- **`pn_manifest`** — the schema manifest. Validates a tree, computes its schema hash, and writes its manifest (see [Schema Manifest](#schema-manifest)).

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
    bool readonly;        // a client's set is refused
    const char *unit;     // int and float: what the value is measured in ("Hz", "dB", "cents"), or NULL
} pn_node_t;
```

The schema describes what each value is: its type, its constraints, and its unit. It never describes how a value is to be shown. Any client, a script or a user interface, decides its own presentation from those facts.

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
- **SchemaGet / Schema** — client asks for a server's schema hash; the server replies with its hash and root name. An empty root addresses every server on the transport, and each replies with its own root, which also reveals which servers share the transport.
- **ManifestGet / ManifestChunk** — client asks the server with the given root for its manifest; the server streams it in chunks and sets `last` on the final one. Chunk boundaries carry no meaning: the manifest is the concatenation of the chunks. See [Schema Manifest](#schema-manifest).

### Type coercion

The server coerces values when the wire type doesn't match the parameter type:

- `int` → `float`, `float` → `int` (truncated)
- `int` → `bool` (nonzero = true)
- `int` → `enum`

Other mismatches (e.g. `string` → `float`) are silently rejected. Values are clamped to the parameter's declared range after coercion.

## Module Pattern

Server modules build their own parameter subtrees using `pn_param_t` arrays embedded in their structs. Inline constructors (`pn_float`, `pn_int`, `pn_bool`, `pn_enum`, `pn_string`, `pn_action`, `pn_group`) simplify tree construction. `pn_readonly()` wraps a constructed node that the server reports and pushes but never accepts a set for, a meter or a measurement; the manifest marks it `ro`. `pn_unit()` wraps an int or float with its unit, a token such as `"Hz"` or `"dB"`, which the manifest writes after the type (`float:Hz`); [MANIFEST.md](MANIFEST.md) lists the recommended spellings.

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
#ifndef PN_SCHEMA_ONLY
static void  set_gain(void *ctx, float val) { ((channel_t *)ctx)->gain = val; }
static float get_gain(void *ctx)            { return ((channel_t *)ctx)->gain; }
#endif

pn_param_t *mixer_build_tree(mixer_t *m) {
    for (int i = 0; i < NUM_CHANNELS; i++) {
        channel_t *ch = &m->channels[i];
        uint32_t c = 0;
        ch->nodes[c++] = pn_float("gain", ch,
            PN_FN(set_gain), PN_FN(get_gain), -72.0f, 6.0f, 0.1f);
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

The `PN_FN()` wrappers and the `PN_SCHEMA_ONLY` guard around the callbacks let the tree be compiled without its bindings; see [Schema-only builds](#schema-only-builds).

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

## Schema Manifest

The schema is the shape of a server's tree: every node's name, type, and constraints. `pn_manifest_write()` renders it as a **manifest**, a canonical text form specified in [MANIFEST.md](MANIFEST.md), and `pn_schema_hash()` computes the **schema hash**, a fingerprint of the manifest. A started server reports the same value through `pn_server_schema_hash()`. Two parties that hold the same hash hold the same schema.

Writing a manifest streams bytes to a sink. The sink has the same signature as the send callbacks, so a transport's send function can serve as one:

```c
static int to_stdout(const uint8_t *buf, uint32_t len, void *ctx) {
    (void)ctx;
    return fwrite(buf, 1, len, stdout) == len ? 0 : -1;
}

pn_manifest_write(root, to_stdout, NULL);
```

The writer reads only names, types, constraints, children, and whether a node is read-only. It never calls a getter or setter and never dereferences a node's `ctx`, which is what makes schema-only builds possible.

### Over the wire

A running server answers two requests. `pn_client_get_schema()` asks for the schema hash, and the reply arrives through the client's `on_schema` callback with the server's hash and root name. A client that knows its schema's hash, for example from the header of a committed manifest, compares the two and decides for itself what a mismatch means; the server does not know or care what the client believes the schema to be. `pn_client_get_manifest()` asks for the manifest itself, delivered through `on_manifest` one chunk at a time with `last` set on the final chunk. This is the route to a manifest when the tree is only known to the running program, and it is the ground truth for what a device is running.

```c
static void on_schema(uint32_t hash, const char *root, void *ctx) {
    if (hash != MY_SCHEMA_HASH)
        fprintf(stderr, "warning: %s schema differs from mine\n", root);
}

pn_client_config_t config = {
    /* ... */
    .on_schema = on_schema,
};
pn_client_get_schema(client, "mixer");
```

### Schema-only builds

A `pn_param_t` carries both the schema and the binding (`ctx`, `set`, `get`). Tree-building code that wraps each callback reference in `PN_FN()` and keeps the callbacks themselves behind `#ifndef PN_SCHEMA_ONLY` can be compiled without its bindings: with `PN_SCHEMA_ONLY` defined, `PN_FN()` expands to `NULL`, and the modules that implement the callbacks need not exist.

```c
#ifndef PN_SCHEMA_ONLY
static void  set_gain(void *ctx, float val) { ((channel_t *)ctx)->gain = val; }
static float get_gain(void *ctx)            { return ((channel_t *)ctx)->gain; }
#endif

ch->nodes[c++] = pn_float("gain", ch, PN_FN(set_gain), PN_FN(get_gain), -72.0f, 6.0f, 0.1f);
```

Since `ctx` is a `void *`, a schema-only caller passes `NULL` and needs none of the module's types.

### Host schema tool

A schema tool is a small executable, built for the development host, that compiles a project's tree-building code with `PN_SCHEMA_ONLY` and writes the manifest to stdout. It links only pn, so it builds without the project's other dependencies and costs the target nothing. [example/mixer_schema.c](example/mixer_schema.c) is the one for the example mixer:

```cmake
add_executable(mixer_schema mixer_schema.c mixer.c)
target_compile_definitions(mixer_schema PRIVATE PN_SCHEMA_ONLY)
target_link_libraries(mixer_schema pn)
```

The same source file is compiled twice: once into the application with its bindings, once into the tool without them.

### Committed manifests

A project can commit its manifest and let a test keep it current. `pn_add_manifest_test()` adds a CTest case that runs a schema tool and compares its output with a committed file:

```cmake
pn_add_manifest_test(NAME mixer_manifest TOOL mixer_schema EXPECTED ${CMAKE_SOURCE_DIR}/mixer.manifest)
```

The comparison is made in canonical form, so the committed file may carry comments and its own whitespace, and the hash it declares must match the tool's. When the test fails, the schema changed: regenerate the file with the tool, and the diff shows the change in review. pn's own example manifests live in [test/fixtures/](test/fixtures/).
