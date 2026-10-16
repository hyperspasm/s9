# pn — JavaScript

The pn client for browsers and Node: the wire format (protobuf messages, CRC-16, COBS framing), the schema manifest, and a client, in one dependency-free ES module, [pn.js](pn.js). It speaks the same bytes as the C and Python implementations: one pn packet per binary websocket message under the subprotocol `pn`.

## Client

```js
import { Client, connect, parseManifest } from "./pn.js";

const manifest = parseManifest(await (await fetch("/mixer.manifest")).text());

const client = new Client({
  onUpdate(path, value, type) { /* type is "bool", "int", "float", "enum", or "string" */ },
});

const link = connect(client, "ws://localhost:8080", {
  async onConnect() {
    if (!await client.checkSchema(manifest)) { /* the server's schema differs; warned by default */ }
    client.get(manifest.root);            // every current value arrives through onUpdate
  },
  onDisconnect() {},
});

client.setFloat("mixer/ch1/gain", -6.0);
client.setBool("mixer/ch1/mute", true);
client.setInt("mixer/ch1/input", 1);
client.setEnum("mixer/ch1/input", 1);
client.setString("mixer/ch1/name", "vocals");
client.action("mixer/ch1/reset");
client.get("mixer/ch1");                  // a group: one update per leaf

const { hash, root } = await client.requestSchema("mixer");
const text = new TextDecoder().decode(await client.requestManifest("mixer"));

link.close();
```

`connect` keeps the client connected, reconnecting after a close, and drops sends while disconnected. It uses the global `WebSocket` (browsers, Node 22 and later); pass another implementation as `WebSocket` in the options. A client can also be bound to any transport by hand: `client.bind(send)` gives it a function that sends one framed packet, and `client.feed(bytes)` hands it received bytes.

`checkSchema` compares a manifest's declared hash (or the hash of its body when it has no header) with what its server reports. How a client responds to a mismatch is its own decision; `onMismatch` is `"warn"` (the default), `"throw"`, or `"ignore"`.

## Manifest

```js
import { parseManifest, schemaHash, canonicalize } from "./pn.js";

const m = parseManifest(text);      // {root, entries, byPath, declaredHash, canonical, schemaHash}
m.byPath.get("mixer/ch1/gain").min  // constraints per entry, as in MANIFEST.md
m.entries[0].readonly               // true for a node the server reports but never sets
schemaHash(text);                   // the hash of any manifest text
```

## Wire format

`encodePacket(message)` and `decodePacket(frame)` convert between framed packets and messages named as in [pn.proto](../proto/pn.proto): `{set: {path, value: {float_val: 1}}}`, `{get: {path}}`, `{schema_get: {root}}`, and so on. `FrameDecoder` splits a byte stream into packets.

## Tests

```bash
node --test test/pn.test.js
```

The tests hold the encoder to the packets in [../test/fixtures/packets.json](../test/fixtures/packets.json), framed by the Python implementation, and the manifest code to the fixtures the C implementation generated. When pn's Python venv exists, one test exchanges messages with a Python server over a websocket.
