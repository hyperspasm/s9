// pn for JavaScript: the wire format (protobuf messages, CRC-16, COBS framing),
// the schema manifest, and a client, for browsers and Node. One pn packet
// travels as one binary websocket message under the subprotocol "pn". No
// dependencies.

export const SUBPROTOCOL = "pn";

const utf8 = new TextEncoder();
const utf8Strict = new TextDecoder("utf-8", { fatal: true });

// --- CRC-16-CCITT (polynomial 0x1021, initial value 0xFFFF)

const CRC_TABLE = (() => {
  const table = new Uint16Array(256);
  for (let i = 0; i < 256; i++) {
    let crc = i << 8;
    for (let bit = 0; bit < 8; bit++) {
      crc = crc & 0x8000 ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff;
    }
    table[i] = crc;
  }
  return table;
})();

export function crc16(bytes) {
  let crc = 0xffff;
  for (const b of bytes) crc = ((crc << 8) ^ CRC_TABLE[((crc >> 8) ^ b) & 0xff]) & 0xffff;
  return crc;
}

// --- Framing: COBS over the payload plus its CRC, terminated by 0x00

export function encodeFrame(payload) {
  const crc = crc16(payload);
  const data = new Uint8Array(payload.length + 2);
  data.set(payload);
  data[payload.length] = crc & 0xff;
  data[payload.length + 1] = crc >> 8;

  const out = [0];
  let codeIndex = 0;
  let code = 1;
  for (const b of data) {
    if (b === 0) {
      out[codeIndex] = code;
      codeIndex = out.length;
      out.push(0);
      code = 1;
    } else {
      out.push(b);
      code++;
      if (code === 0xff) {
        out[codeIndex] = code;
        codeIndex = out.length;
        out.push(0);
        code = 1;
      }
    }
  }
  out[codeIndex] = code;
  out.push(0);
  return Uint8Array.from(out);
}

// Decode one frame given without its terminator. Throws on bad framing or CRC.
export function decodeFrame(frame) {
  const out = [];
  let pos = 0;
  while (pos < frame.length) {
    const code = frame[pos++];
    if (code === 0) throw new Error("unexpected zero in COBS frame");
    const count = code - 1;
    if (pos + count > frame.length) throw new Error("COBS frame truncated");
    for (let i = 0; i < count; i++) out.push(frame[pos + i]);
    pos += count;
    if (code < 0xff && pos < frame.length) out.push(0);
  }
  if (out.length < 2) throw new Error("COBS frame too short for CRC");
  const payload = Uint8Array.from(out.slice(0, -2));
  const received = out[out.length - 2] | (out[out.length - 1] << 8);
  if (received !== crc16(payload)) throw new Error("CRC mismatch");
  return payload;
}

// Splits a byte stream into frames and hands each valid payload to onPacket.
// A frame longer than maxFrame bytes is dropped.
export class FrameDecoder {
  constructor(onPacket, maxFrame = 1024) {
    this.onPacket = onPacket;
    this.maxFrame = maxFrame;
    this.buf = [];
    this.overflow = false;
  }

  feed(bytes) {
    for (const b of bytes) {
      if (b === 0) {
        if (this.buf.length && !this.overflow) {
          let payload = null;
          try { payload = decodeFrame(Uint8Array.from(this.buf)); } catch { /* dropped */ }
          if (payload) this.onPacket(payload);
        }
        this.buf.length = 0;
        this.overflow = false;
      } else if (this.buf.length < this.maxFrame) {
        this.buf.push(b);
      } else {
        this.overflow = true;
      }
    }
  }
}

// --- Protobuf, for the pn message set only

const VARINT = 0, LEN = 2, FIXED32 = 5;

class Writer {
  constructor() { this.bytes = []; }

  varint(value) {
    let v = BigInt(value);
    while (v >= 0x80n) {
      this.bytes.push(Number(v & 0x7fn) | 0x80);
      v >>= 7n;
    }
    this.bytes.push(Number(v));
  }

  tag(field, wireType) { this.varint((field << 3) | wireType); }
  uint32(field, n) { this.tag(field, VARINT); this.varint(n >>> 0); }
  int32(field, n) { this.tag(field, VARINT); this.varint(BigInt.asUintN(64, BigInt(n))); }
  bool(field, b) { this.tag(field, VARINT); this.varint(b ? 1 : 0); }

  fixed32(field, n) {
    this.tag(field, FIXED32);
    const view = new DataView(new ArrayBuffer(4));
    view.setUint32(0, n >>> 0, true);
    this.bytes.push(...new Uint8Array(view.buffer));
  }

  float(field, f) {
    this.tag(field, FIXED32);
    const view = new DataView(new ArrayBuffer(4));
    view.setFloat32(0, f, true);
    this.bytes.push(...new Uint8Array(view.buffer));
  }

  bytesField(field, bytes) {
    this.tag(field, LEN);
    this.varint(bytes.length);
    for (const b of bytes) this.bytes.push(b);
  }

  string(field, s) { this.bytesField(field, utf8.encode(s)); }
  message(field, writer) { this.bytesField(field, writer.bytes); }
  finish() { return Uint8Array.from(this.bytes); }
}

class Reader {
  constructor(bytes) { this.b = bytes; this.pos = 0; }
  get done() { return this.pos >= this.b.length; }

  varint() {
    let result = 0n, shift = 0n;
    for (;;) {
      if (this.pos >= this.b.length) throw new Error("truncated varint");
      const byte = this.b[this.pos++];
      result |= BigInt(byte & 0x7f) << shift;
      if (!(byte & 0x80)) return result;
      shift += 7n;
      if (shift > 63n) throw new Error("varint too long");
    }
  }

  key() {
    const key = Number(this.varint());
    return [key >> 3, key & 7];
  }

  fixed32() {
    if (this.pos + 4 > this.b.length) throw new Error("truncated fixed32");
    const view = new DataView(this.b.buffer, this.b.byteOffset + this.pos, 4);
    this.pos += 4;
    return view;
  }

  bytes() {
    const n = Number(this.varint());
    if (this.pos + n > this.b.length) throw new Error("truncated field");
    const out = this.b.subarray(this.pos, this.pos + n);
    this.pos += n;
    return out;
  }

  skip(wireType) {
    if (wireType === VARINT) this.varint();
    else if (wireType === FIXED32) this.fixed32();
    else if (wireType === LEN) this.bytes();
    else if (wireType === 1) this.pos += 8;
    else throw new Error(`unsupported wire type ${wireType}`);
  }
}

// Messages are plain objects named as in pn.proto: {set: {path, value: {float_val: 1}}}
// and so on. Exactly one of the envelope's members is present.

const VALUE_KINDS = ["bool_val", "int_val", "float_val", "enum_val", "string_val"];

function encodeValue(v) {
  const w = new Writer();
  if ("bool_val" in v) w.bool(1, v.bool_val);
  else if ("int_val" in v) w.int32(2, v.int_val);
  else if ("float_val" in v) w.float(3, v.float_val);
  else if ("enum_val" in v) w.uint32(4, v.enum_val);
  else if ("string_val" in v) w.string(5, v.string_val);
  else throw new Error("value has no kind");
  return w;
}

function encodePathValue(m) {
  const w = new Writer();
  if (m.path) w.string(1, m.path);
  if (m.value) w.message(2, encodeValue(m.value));
  return w;
}

function encodeRoot(m) {
  const w = new Writer();
  if (m.root) w.string(1, m.root);
  return w;
}

const ENVELOPE = {
  set: [1, encodePathValue],
  get: [2, encodeRoot],
  update: [3, encodePathValue],
  schema_get: [4, encodeRoot],
  schema: [5, m => { const w = new Writer(); if (m.hash) w.fixed32(1, m.hash); if (m.root) w.string(2, m.root); return w; }],
  manifest_get: [6, encodeRoot],
  manifest_chunk: [7, m => { const w = new Writer(); if (m.data && m.data.length) w.bytesField(1, m.data); if (m.last) w.bool(2, true); return w; }],
};
// get carries a path, not a root
ENVELOPE.get[1] = m => { const w = new Writer(); if (m.path) w.string(1, m.path); return w; };

export function messageKind(msg) {
  const kinds = Object.keys(msg).filter(k => k in ENVELOPE && msg[k] != null);
  if (kinds.length !== 1) throw new Error("a message has exactly one kind");
  return kinds[0];
}

export function encodeMessage(msg) {
  const kind = messageKind(msg);
  const [field, encode] = ENVELOPE[kind];
  const w = new Writer();
  w.message(field, encode(msg[kind]));
  return w.finish();
}

function decodeValue(bytes) {
  const r = new Reader(bytes);
  const v = {};
  while (!r.done) {
    const [field, wt] = r.key();
    if (field === 1 && wt === VARINT) v.bool_val = r.varint() !== 0n;
    else if (field === 2 && wt === VARINT) v.int_val = Number(BigInt.asIntN(32, r.varint()));
    else if (field === 3 && wt === FIXED32) v.float_val = r.fixed32().getFloat32(0, true);
    else if (field === 4 && wt === VARINT) v.enum_val = Number(r.varint() & 0xffffffffn);
    else if (field === 5 && wt === LEN) v.string_val = utf8Strict.decode(r.bytes());
    else r.skip(wt);
  }
  return v;
}

function decodePathValue(bytes) {
  const r = new Reader(bytes);
  const m = { path: "" };
  while (!r.done) {
    const [field, wt] = r.key();
    if (field === 1 && wt === LEN) m.path = utf8Strict.decode(r.bytes());
    else if (field === 2 && wt === LEN) m.value = decodeValue(r.bytes());
    else r.skip(wt);
  }
  return m;
}

function decodeStrings(bytes, names) {
  const r = new Reader(bytes);
  const m = {};
  for (const name of Object.values(names)) if (typeof name === "string") m[name] = "";
  while (!r.done) {
    const [field, wt] = r.key();
    if (names[field] && wt === LEN) m[names[field]] = utf8Strict.decode(r.bytes());
    else r.skip(wt);
  }
  return m;
}

function decodeSchema(bytes) {
  const r = new Reader(bytes);
  const m = { hash: 0, root: "" };
  while (!r.done) {
    const [field, wt] = r.key();
    if (field === 1 && wt === FIXED32) m.hash = r.fixed32().getUint32(0, true);
    else if (field === 2 && wt === LEN) m.root = utf8Strict.decode(r.bytes());
    else r.skip(wt);
  }
  return m;
}

function decodeChunk(bytes) {
  const r = new Reader(bytes);
  const m = { data: new Uint8Array(0), last: false };
  while (!r.done) {
    const [field, wt] = r.key();
    if (field === 1 && wt === LEN) m.data = Uint8Array.from(r.bytes());
    else if (field === 2 && wt === VARINT) m.last = r.varint() !== 0n;
    else r.skip(wt);
  }
  return m;
}

const DECODERS = {
  1: ["set", decodePathValue],
  2: ["get", b => decodeStrings(b, { 1: "path" })],
  3: ["update", decodePathValue],
  4: ["schema_get", b => decodeStrings(b, { 1: "root" })],
  5: ["schema", decodeSchema],
  6: ["manifest_get", b => decodeStrings(b, { 1: "root" })],
  7: ["manifest_chunk", decodeChunk],
};

export function decodeMessage(bytes) {
  const r = new Reader(bytes);
  const msg = {};
  while (!r.done) {
    const [field, wt] = r.key();
    if (DECODERS[field] && wt === LEN) {
      const [name, decode] = DECODERS[field];
      msg[name] = decode(r.bytes());
    } else {
      r.skip(wt);
    }
  }
  return msg;
}

export function encodePacket(msg) { return encodeFrame(encodeMessage(msg)); }
export function decodePacket(frame) { return decodeMessage(decodeFrame(frame)); }

// --- Manifest (see MANIFEST.md)

export class ManifestError extends Error {}

export function fnv1a32(bytes) {
  let h = 0x811c9dc5;
  for (const b of bytes) h = Math.imul(h ^ b, 0x01000193) >>> 0;
  return h >>> 0;
}

function toBytes(text) {
  return typeof text === "string" ? utf8.encode(text) : text;
}

const isSpace = b => b === 0x20 || b === 0x09 || b === 0x0d;

// The canonical form: comments and blank lines removed, whitespace collapsed.
// Byte-wise, so that only ASCII whitespace separates fields.
export function canonicalize(text) {
  const bytes = toBytes(text);
  const out = [];
  let start = 0;
  for (let i = 0; i <= bytes.length; i++) {
    if (i < bytes.length && bytes[i] !== 0x0a) continue;
    let end = start;
    while (end < i && bytes[end] !== 0x23) end++;
    const fields = [];
    let j = start;
    while (j < end) {
      while (j < end && isSpace(bytes[j])) j++;
      const f = j;
      while (j < end && !isSpace(bytes[j])) j++;
      if (j > f) fields.push(bytes.subarray(f, j));
    }
    if (fields.length) {
      fields.forEach((f, k) => { if (k) out.push(0x20); for (const b of f) out.push(b); });
      out.push(0x0a);
    }
    start = i + 1;
  }
  return Uint8Array.from(out);
}

export function schemaHash(text) { return fnv1a32(canonicalize(text)); }

const TYPES = ["action", "bool", "int", "float", "enum", "string"];
const INT32 = /^-?(0|[1-9][0-9]*)$/, UINT32 = /^(0|[1-9][0-9]*)$/, F32 = /^0x[0-9a-f]{8}$/;
const HEADER = /^# pn manifest v1 (\S+) fnv1a32=([0-9a-f]{8})[ \t\r]*$/;

function nameProblem(name) {
  if (!name.length) return "is empty";
  for (const b of utf8.encode(name)) {
    if (b <= 0x20 || b === 0x7f || b === 0x2f || b === 0x23 || b === 0x7c) return "contains a forbidden character";
  }
  return null;
}

function parseInt32(field, where) {
  if (!INT32.test(field)) throw new ManifestError(`${where}: ${field} is not an integer`);
  const v = Number(field);
  if (v < -(2 ** 31) || v >= 2 ** 31) throw new ManifestError(`${where}: ${field} is out of the int32 range`);
  return v;
}

function parseF32(field, where) {
  if (!F32.test(field)) throw new ManifestError(`${where}: ${field} is not a binary32 bit pattern`);
  const view = new DataView(new ArrayBuffer(4));
  view.setUint32(0, parseInt(field.slice(2), 16), false);
  return view.getFloat32(0, false);
}

function parseEntry(line) {
  let fields;
  try { fields = utf8Strict.decode(line).split(" "); } catch { throw new ManifestError("invalid UTF-8"); }
  if (fields.length < 2) throw new ManifestError(`${fields[0]}: expected a path and a type`);
  const [path, type, ...meta] = fields;
  for (const segment of path.split("/")) {
    const problem = nameProblem(segment);
    if (problem) throw new ManifestError(`${path}: name ${segment} ${problem}`);
  }
  if (!TYPES.includes(type)) throw new ManifestError(`${path}: unknown type ${type}`);
  const expect = n => { if (meta.length !== n) throw new ManifestError(`${path}: ${type} takes ${n} constraint fields, got ${meta.length}`); };
  const entry = { path, type, name: path.slice(path.lastIndexOf("/") + 1) };
  if (type === "int") {
    expect(3);
    [entry.min, entry.max, entry.step] = meta.map(f => parseInt32(f, path));
  } else if (type === "float") {
    expect(3);
    [entry.min, entry.max, entry.step] = meta.map(f => parseF32(f, path));
  } else if (type === "enum") {
    expect(1);
    entry.labels = meta[0].split("|");
    for (const label of entry.labels) {
      const problem = nameProblem(label);
      if (problem) throw new ManifestError(`${path}: label ${label} ${problem}`);
    }
  } else if (type === "string") {
    expect(1);
    if (!UINT32.test(meta[0])) throw new ManifestError(`${path}: ${meta[0]} is not an unsigned integer`);
    entry.max_len = Number(meta[0]);
  } else {
    expect(0);
  }
  return entry;
}

// Read a manifest: {root, entries, byPath, declaredHash (or null), canonical, schemaHash}
export function parseManifest(text) {
  const bytes = toBytes(text);
  let firstLine = bytes;
  const nl = bytes.indexOf(0x0a);
  if (nl >= 0) firstLine = bytes.subarray(0, nl);
  let header = null;
  try { header = HEADER.exec(utf8Strict.decode(firstLine)); } catch { throw new ManifestError("header is not valid UTF-8"); }
  const headerRoot = header ? header[1] : null;
  const declaredHash = header ? parseInt(header[2], 16) : null;

  const canonical = canonicalize(bytes);
  const entries = [];
  let start = 0;
  for (let i = 0; i < canonical.length; i++) {
    if (canonical[i] !== 0x0a) continue;
    entries.push(parseEntry(canonical.subarray(start, i)));
    start = i + 1;
  }

  const roots = new Set(entries.map(e => e.path.split("/")[0]));
  if (roots.size > 1) throw new ManifestError(`entries under more than one root: ${[...roots].sort()}`);
  if (headerRoot !== null && roots.size && !roots.has(headerRoot)) throw new ManifestError(`header root ${headerRoot} does not match the entries`);
  const root = headerRoot ?? (roots.size ? [...roots][0] : null);
  if (root === null) throw new ManifestError("no header and no entries");

  return {
    root, entries, declaredHash, canonical,
    schemaHash: fnv1a32(canonical),
    byPath: new Map(entries.map(e => [e.path, e])),
  };
}

// --- Client

export class SchemaMismatch extends Error {}

function updateOf(value) {
  for (const kind of VALUE_KINDS) if (kind in value) return [kind.slice(0, -4), value[kind]];
  return null;
}

// Sends requests through a send function and delivers replies through
// callbacks or promises. Bind it to a transport with bind() and feed() or use
// connect().
export class Client {
  constructor({ onUpdate = null, onSchema = null, onManifest = null } = {}) {
    this.onUpdate = onUpdate;
    this.onSchema = onSchema;
    this.onManifest = onManifest;
    this._send = null;
    this._decoder = new FrameDecoder(p => this._onPacket(p));
    this._manifestChunks = [];
    this._schemaWaiters = [];
    this._manifestWaiters = [];
  }

  // send: called with one framed packet as a Uint8Array
  bind(send) { this._send = send; }
  feed(bytes) { this._decoder.feed(bytes); }

  setFloat(path, v) { this._sendMessage({ set: { path, value: { float_val: v } } }); }
  setInt(path, v) { this._sendMessage({ set: { path, value: { int_val: v } } }); }
  setBool(path, v) { this._sendMessage({ set: { path, value: { bool_val: v } } }); }
  setEnum(path, v) { this._sendMessage({ set: { path, value: { enum_val: v } } }); }
  setString(path, v) { this._sendMessage({ set: { path, value: { string_val: v } } }); }
  get(path) { this._sendMessage({ get: { path } }); }
  action(path) { this._sendMessage({ set: { path } }); }

  // root names the server; "" asks every server on the transport
  getSchema(root = "") { this._sendMessage({ schema_get: { root } }); }
  getManifest(root) { this._manifestChunks = []; this._sendMessage({ manifest_get: { root } }); }

  requestSchema(root = "", timeoutMs = 2000) {
    return this._request(this._schemaWaiters, () => this.getSchema(root), timeoutMs, "no schema reply");
  }

  requestManifest(root, timeoutMs = 5000) {
    return this._request(this._manifestWaiters, () => this.getManifest(root), timeoutMs, "no manifest reply");
  }

  // Compare a manifest (parsed or text) with the hash its server reports.
  // Its root selects the server; its declared hash is compared, or the hash of
  // its body when it has no header. Returns true on a match. On a mismatch,
  // onMismatch selects the response: "warn" logs and returns false, "throw"
  // throws SchemaMismatch, "ignore" returns false.
  async checkSchema(manifest, { onMismatch = "warn", timeoutMs = 2000 } = {}) {
    if (typeof manifest === "string" || manifest instanceof Uint8Array) manifest = parseManifest(manifest);
    const expected = manifest.declaredHash ?? manifest.schemaHash;
    const { hash, root } = await this.requestSchema(manifest.root, timeoutMs);
    if (hash === expected) return true;
    const message = `schema mismatch for ${root}: server reports ${hex8(hash)}, manifest declares ${hex8(expected)}`;
    if (onMismatch === "throw") throw new SchemaMismatch(message);
    if (onMismatch === "warn") console.warn(message);
    return false;
  }

  _request(waiters, send, timeoutMs, what) {
    return new Promise((resolve, reject) => {
      const waiter = { resolve, reject };
      waiters.push(waiter);
      waiter.timer = setTimeout(() => {
        const i = waiters.indexOf(waiter);
        if (i >= 0) waiters.splice(i, 1);
        reject(new Error(what));
      }, timeoutMs);
      send();
    });
  }

  _resolve(waiters, result) {
    for (const w of waiters.splice(0)) { clearTimeout(w.timer); w.resolve(result); }
  }

  _sendMessage(msg) {
    if (this._send) this._send(encodePacket(msg));
  }

  _onPacket(payload) {
    let msg;
    try { msg = decodeMessage(payload); } catch { return; }
    if (msg.update) {
      const u = msg.update.value && updateOf(msg.update.value);
      if (u && this.onUpdate) this.onUpdate(msg.update.path, u[1], u[0]);
    } else if (msg.schema) {
      this._resolve(this._schemaWaiters, msg.schema);
      if (this.onSchema) this.onSchema(msg.schema.hash, msg.schema.root);
    } else if (msg.manifest_chunk) {
      this._manifestChunks.push(msg.manifest_chunk.data);
      if (msg.manifest_chunk.last) {
        const total = this._manifestChunks.reduce((n, c) => n + c.length, 0);
        const data = new Uint8Array(total);
        let pos = 0;
        for (const c of this._manifestChunks) { data.set(c, pos); pos += c.length; }
        this._manifestChunks = [];
        this._resolve(this._manifestWaiters, data);
        if (this.onManifest) this.onManifest(data);
      }
    }
  }
}

export function hex8(n) { return (n >>> 0).toString(16).padStart(8, "0"); }

// Connect a client over a websocket and keep it connected: after the
// connection closes, reconnect every retryMs. Sends while disconnected are
// dropped. Returns {close()}.
export function connect(client, url, { WebSocket: Impl = globalThis.WebSocket, retryMs = 1000, onConnect = null, onDisconnect = null } = {}) {
  let ws = null;
  let closed = false;
  let timer = null;

  client.bind(bytes => { if (ws && ws.readyState === 1) ws.send(bytes); });

  const open = () => {
    if (closed) return;
    const socket = new Impl(url, [SUBPROTOCOL]);
    socket.binaryType = "arraybuffer";
    let wasOpen = false;
    socket.onopen = () => { ws = socket; wasOpen = true; if (onConnect) onConnect(); };
    socket.onmessage = event => { if (event.data instanceof ArrayBuffer) client.feed(new Uint8Array(event.data)); };
    socket.onerror = () => {};
    socket.onclose = () => {
      if (ws === socket) ws = null;
      if (wasOpen && onDisconnect) onDisconnect();
      if (!closed) timer = setTimeout(open, retryMs);
    };
  };
  open();

  return {
    close() {
      closed = true;
      clearTimeout(timer);
      if (ws) ws.close();
    },
  };
}
