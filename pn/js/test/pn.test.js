// Tests for pn.js: the wire format against the packets the Python
// implementation frames, the manifest against the C-generated fixtures, the
// client, and, when pn's Python venv exists, a live exchange with a Python
// server over a websocket.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync, existsSync } from "node:fs";
import { spawn } from "node:child_process";
import { createServer } from "node:net";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

import {
  crc16, encodeFrame, decodeFrame, FrameDecoder,
  encodeMessage, decodeMessage, encodePacket, decodePacket,
  fnv1a32, canonicalize, schemaHash, parseManifest, ManifestError,
  Client, SchemaMismatch, connect, hex8,
} from "../pn.js";

const here = dirname(fileURLToPath(import.meta.url));
const fixtures = join(here, "..", "..", "test", "fixtures");
const packets = JSON.parse(readFileSync(join(fixtures, "packets.json"), "utf8"));
const hex = bytes => Array.from(bytes, b => b.toString(16).padStart(2, "0")).join("");
const unhex = s => Uint8Array.from(s.match(/../g), h => parseInt(h, 16));
const text = new TextEncoder();

// The message a fixture describes, as pn.js represents it
function expected(desc) {
  const msg = structuredClone(desc);
  const inner = msg[Object.keys(msg)[0]];
  if (inner.value && "float_val" in inner.value) inner.value.float_val = Math.fround(inner.value.float_val);
  if ("data" in inner) inner.data = text.encode(inner.data);
  return msg;
}

test("crc16 matches the CCITT check value", () => {
  assert.equal(crc16(text.encode("123456789")), 0x29b1);
  assert.equal(crc16(new Uint8Array(0)), 0xffff);
});

test("framing round-trips and rejects corruption", () => {
  const payload = Uint8Array.from([0, 1, 2, 0, 0, 255, 3]);
  const frame = encodeFrame(payload);
  assert.equal(frame.at(-1), 0);
  assert.equal(frame.subarray(0, -1).indexOf(0), -1, "no zero inside the frame");
  assert.deepEqual(decodeFrame(frame.subarray(0, -1)), payload);
  const bad = frame.slice(0, -1);
  bad[2] ^= 1;
  assert.throws(() => decodeFrame(bad), /CRC/);
});

test("every fixture packet decodes to its message and re-encodes byte for byte", () => {
  for (const { name, frame, message } of packets) {
    const bytes = unhex(frame);
    assert.deepEqual(decodePacket(bytes.subarray(0, -1)), expected(message), name);
    assert.equal(hex(encodePacket(expected(message))), frame, name);
  }
});

test("the frame decoder splits a stream and drops oversized frames", () => {
  const got = [];
  const dec = new FrameDecoder(p => got.push(decodeMessage(p)), 64);
  const stream = new Uint8Array([...unhex(packets[0].frame), ...unhex(packets[10].frame)]);
  for (const b of stream) dec.feed([b]);   // one byte at a time
  assert.deepEqual(got, [expected(packets[0].message), expected(packets[10].message)]);
  const big = encodeFrame(new Uint8Array(100));
  dec.feed(big);
  dec.feed(unhex(packets[10].frame));
  assert.equal(got.length, 3);
});

test("fnv1a32 vectors", () => {
  assert.equal(fnv1a32(new Uint8Array(0)), 0x811c9dc5);
  assert.equal(fnv1a32(text.encode("a")), 0xe40c292c);
  assert.equal(fnv1a32(text.encode("foobar")), 0xbf9cf968);
});

test("canonicalization ignores comments, blank lines, and whitespace, but not Unicode whitespace", () => {
  assert.deepEqual(canonicalize("# h\r\n\r\n  a/b\tint   1 2 3   # t\r\n a/c bool \n"), text.encode("a/b int 1 2 3\na/c bool\n"));
  assert.deepEqual(canonicalize(""), new Uint8Array(0));
  const nbsp = "r/a b bool\n";
  assert.deepEqual(canonicalize(nbsp), text.encode(nbsp));
});

test("the fixture manifests parse and hash to their declared hashes", () => {
  for (const [file, root, count] of [["amp.manifest", "amp", 8], ["mixer.manifest", "mixer", 58]]) {
    const m = parseManifest(readFileSync(join(fixtures, file)));
    assert.equal(m.root, root);
    assert.equal(m.entries.length, count);
    assert.equal(m.schemaHash, m.declaredHash, file);
  }
  const amp = parseManifest(readFileSync(join(fixtures, "amp.manifest")));
  assert.equal(amp.declaredHash, 0x76a2b061);
  const by = amp.byPath;
  assert.deepEqual([...by.keys()], ["amp/power", "amp/volume", "amp/eq/bass", "amp/eq/treble", "amp/input", "amp/name", "amp/level", "amp/reset"]);
  assert.equal(by.get("amp/level").readonly, true);
  assert.equal(by.get("amp/volume").readonly, false);
  assert.equal(by.get("amp/power").type, "bool");
  assert.deepEqual([by.get("amp/volume").min, by.get("amp/volume").max, by.get("amp/volume").step], [0, 100, 1]);
  assert.deepEqual([by.get("amp/eq/bass").min, by.get("amp/eq/bass").max, by.get("amp/eq/bass").step], [-12, 12, 0.5]);
  assert.deepEqual(by.get("amp/input").labels, ["mic", "line", "aux"]);
  assert.equal(by.get("amp/name").max_len, 32);
  assert.equal(by.get("amp/reset").type, "action");
  assert.equal(by.get("amp/eq/bass").name, "bass");
  assert.deepEqual([by.get("amp/volume").unit, by.get("amp/eq/bass").unit, by.get("amp/level").unit], ["%", "dB", undefined]);
});

test("a hand-annotated manifest has the same hash", () => {
  const annotated = "# pn manifest v1 amp fnv1a32=76a2b061\r\n\r\n# Front\r\namp/power\tbool\r\namp/volume   int:% 0 100 1  # percent\r\n"
    + "amp/eq/bass float:dB 0xc1400000 0x41400000 0x3f000000\r\namp/eq/treble float:dB 0xc1400000 0x41400000 0x3f000000\r\n"
    + "amp/input enum mic|line|aux\r\namp/name string 32\r\namp/level float 0x00000000 0x3f800000 0x00000000  ro  # a meter\r\namp/reset action\r\n";
  const m = parseManifest(annotated);
  assert.equal(m.schemaHash, 0x76a2b061);
  assert.equal(m.entries.length, 8);
  assert.equal(schemaHash(annotated), 0x76a2b061);
  assert.equal(parseManifest("r/a bool\n").declaredHash, null);
});

test("a unit is read from an int or a float", () => {
  const m = parseManifest("r/f float:Hz 0x00000000 0x3f800000 0x00000000\nr/i int:cents 0 1 1 ro\nr/plain int 0 1 1\n");
  assert.deepEqual(m.entries.map(e => e.unit), ["Hz", "cents", undefined]);
});

test("a read-only marker is read", () => {
  const m = parseManifest("r/meter float 0x00000000 0x3f800000 0x00000000 ro\nr/gain float 0x00000000 0x3f800000 0x00000000\nr/on bool ro\n");
  assert.deepEqual(m.entries.map(e => e.readonly), [true, false, true]);
});

test("malformed manifests are rejected", () => {
  for (const bad of ["r/a nonsense\n", "r/a int 0 1\n", "r/a float 0x3F800000 0x3f800000 0x00000000\n", "r/a enum mic|li/ne\n",
                     "r/a bool\ns/b bool\n", "# pn manifest v1 s fnv1a32=00000000\nr/a bool\n", "\n", "r/go action ro\n", "r/a bool ro ro\n",
                     "r/a bool:dB\n", "r/a int: 0 1 1\n", "r/a int:a:b 0 1 1\n", "r/a float:d#B 0x00000000 0x3f800000 0x00000000\n"]) {
    assert.throws(() => parseManifest(bad), ManifestError, JSON.stringify(bad));
  }
  assert.throws(() => parseManifest(Uint8Array.from([0x72, 0x2f, 0xff, 0x20, 0x62, 0x6f, 0x6f, 0x6c, 0x0a])), ManifestError);
});

test("the client frames requests and delivers replies", async () => {
  const sent = [];
  const updates = [];
  const client = new Client({ onUpdate: (path, value, type) => updates.push([path, value, type]) });
  client.bind(bytes => sent.push(hex(bytes)));

  client.setFloat("amp/eq/bass", -3.5);
  client.setInt("amp/volume", -7);
  client.setBool("amp/power", false);
  client.setEnum("amp/input", 2);
  client.setString("amp/name", "µs");
  client.action("amp/reset");
  client.get("amp");
  client.getSchema("amp");
  client.getManifest("amp");
  const byName = Object.fromEntries(packets.map(p => [p.name, p.frame]));
  assert.deepEqual(sent, ["set_float", "set_int_negative", "set_bool_false", "set_enum", "set_string", "action", "get", "schema_get", "manifest_get"].map(n => byName[n]));

  client.feed(unhex(byName.update_float));
  client.feed(unhex(byName.update_string));
  assert.deepEqual(updates, [["amp/eq/bass", 0.25, "float"], ["amp/name", "vocals", "string"]]);

  const schema = client.requestSchema("amp");
  client.feed(unhex(byName.schema));
  assert.deepEqual(await schema, { hash: 0xfb9c41f2, root: "amp" });

  const manifest = client.requestManifest("amp");
  client.feed(unhex(byName.manifest_chunk));
  client.feed(unhex(byName.manifest_chunk_last));
  assert.equal(new TextDecoder().decode(await manifest), "abc# pn manifest v1 amp fnv1a32=fb9c41f2\n");

  await assert.rejects(client.requestSchema("amp", 20), /no schema reply/);
});

test("checkSchema compares the declared hash with the server's", async () => {
  const client = new Client();
  const reply = hash => client.bind(() => client.feed(encodePacket({ schema: { hash, root: "amp" } })));
  const manifest = readFileSync(join(fixtures, "amp.manifest"), "utf8");
  reply(0x76a2b061);
  assert.equal(await client.checkSchema(manifest), true);
  reply(0x12345678);
  assert.equal(await client.checkSchema(manifest, { onMismatch: "ignore" }), false);
  await assert.rejects(client.checkSchema(manifest, { onMismatch: "throw" }), SchemaMismatch);
  assert.equal(hex8(0x09decefb), "09decefb");
});

// --- Against the Python implementation

const python = join(here, "..", "..", "venv", "bin", "python3");

async function freePort() {
  return new Promise(resolve => {
    const srv = createServer();
    srv.listen(0, "127.0.0.1", () => { const { port } = srv.address(); srv.close(() => resolve(port)); });
  });
}

function startPythonServer(port) {
  return new Promise((resolve, reject) => {
    const proc = spawn(python, [join(here, "pn_server.py"), String(port)], { stdio: ["ignore", "pipe", "inherit"] });
    proc.stdout.on("data", d => { if (d.toString().includes("ready")) resolve(proc); });
    proc.on("exit", code => reject(new Error(`python server exited with ${code}`)));
  });
}

test("a live exchange with a Python server over a websocket", { skip: !existsSync(python) && "pn's Python venv is not present" }, async () => {
  const port = await freePort();
  const proc = await startPythonServer(port);
  try {
    const manifest = parseManifest(readFileSync(join(fixtures, "amp.manifest")));
    const updates = [[], []];
    const clients = updates.map(u => new Client({ onUpdate: (p, v) => u.push([p, v]) }));
    const links = [];
    for (const c of clients) {
      await new Promise(resolve => links.push(connect(c, `ws://127.0.0.1:${port}`, { onConnect: resolve })));
    }
    const [a, b] = clients;
    assert.deepEqual(await a.requestSchema("amp"), { hash: manifest.declaredHash, root: "amp" });
    assert.equal(await a.checkSchema(manifest), true);
    const text = new TextDecoder().decode(await a.requestManifest("amp"));
    assert.equal(text, readFileSync(join(fixtures, "amp.manifest"), "utf8"));

    // a change from one client is echoed to both, clamped by the server
    a.setInt("amp/volume", 250);
    const until = (arr, n) => new Promise(resolve => { const tick = () => arr.length >= n ? resolve() : setTimeout(tick, 5); tick(); });
    await Promise.all(updates.map(u => until(u, 1)));
    assert.deepEqual(updates[0].at(-1), ["amp/volume", 100]);
    assert.deepEqual(updates[1].at(-1), ["amp/volume", 100]);

    b.action("amp/reset");
    await Promise.all(updates.map(u => until(u, 2)));
    assert.deepEqual(updates[0].at(-1), ["amp/volume", 0]);
    for (const l of links) l.close();
  } finally {
    proc.kill();
  }
});
