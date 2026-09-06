# pn manifest and schema hash — work plan

Transient working notes for the `pn` branch. Delete before merging to `develop`. Durable documentation must not reference this file.

## Goal

Give pn a canonical text manifest of a server's schema and a hash of it, so that a project can commit a diffable declaration of its schema, a client can detect that its schema differs from the server's, and tooling can read a schema offline. The format is specified in pn/MANIFEST.md. Everything below is implementation, integration, and documentation placement.

## Decisions

- The schema stays defined in code. pn stays a subdirectory of s9 until this work settles; factor it out afterwards.
- Hash: FNV-1a 32-bit over the canonical form. The header carries it, so consumers need no hash function.
- The generator is a byte stream to a sink; the hash is the generator pointed at a hashing sink. Consequence: manifest support costs nothing beyond the hash.
- Generator constraints: no heap, no printf family, no buffer beyond a PN_MAX_PATH stack path buffer, never calls get/set, never dereferences ctx. Two traversals: one to hash for the header, one to emit.
- The server computes the hash in pn_server_start() and caches it.
- Tree validity (MANIFEST.md "Tree requirements", plus PN_MAX_DEPTH and PN_MAX_PATH) is checked in pn_server_start(), which fails on violation. UTF-8 validity is not checked in C: names are compile-time literals, and the Python parser rejects invalid sequences when it decodes.
- Mismatch policy is the client's decision; library clients default to warn.
- Manifest over the wire is a development feature, compile-time optional, default on.
- s9 dogfoods the embedded pattern: a separate host schema tool, not a flag on the main binary.
- Manifest file name in s9: `s9.manifest` at the repo root.

## API sketches

C, pn_manifest.h:

```c
typedef int (*pn_sink_t)(const uint8_t *buf, uint32_t len, void *ctx);  // same signature as send callbacks
pn_schema_result_t pn_schema_validate(const pn_param_t *root, const pn_param_t **offender);
int pn_schema_hash(const pn_param_t *root, uint32_t *hash);
int pn_manifest_write(const pn_param_t *root, pn_sink_t sink, void *ctx);
```

Validation lives in the manifest module because it owns the name rules; the server start calls it. The hash and the writer validate first, so neither produces output for an invalid tree. The result enum names the violated rule and the offender pointer identifies the node, so a failed start can say why.

C, pn_server.h:

```c
uint32_t pn_server_schema_hash(const pn_server_t *server);
```

Schema-only builds, in a public pn header:

```c
#ifdef PN_SCHEMA_ONLY
#define PN_FN(fn) NULL
#else
#define PN_FN(fn) (fn)
#endif
```

Tree-building code wraps every callback reference in PN_FN() and puts the callbacks and their includes behind `#ifndef PN_SCHEMA_ONLY`. Compiled with PN_SCHEMA_ONLY the file depends only on pn; ctx is a void pointer, so a schema-only caller passes NULL. The storage struct (pn_param_t arrays) is public and has no invariant beyond "call build before use"; the object owning the server and transport stays private and embeds one.

Python, pn.manifest:

```python
dump(tree, root) -> str
parse(text) -> Manifest          # entries + declared hash
canonicalize(text_or_bytes) -> bytes
hash(text_or_bytes) -> int
```

canonicalize and hash encode str as UTF-8 and operate on bytes; only ASCII whitespace is whitespace. Do not use the defaults of str.split() or str.strip(), which treat Unicode whitespace as whitespace. This constraint is a comment at the canonicalize function, not documentation.

Clients:

- C: pn_client_get_schema(), plus an on_schema callback in pn_client_config_t.
- Python: `await client.get_schema()`, `client.check_manifest(manifest, on_mismatch="warn" | "raise")`.

## Protocol additions (pn.proto)

```protobuf
message SchemaGet     { string root = 1; }                  // empty: every server on the transport answers
message Schema        { fixed32 hash = 1; string root = 2; }
message ManifestGet   { string root = 1; }                  // required: only the matching server answers
message ManifestChunk { bytes data = 1; bool last = 2; }
```

Envelope: schema_get = 4, schema = 5, manifest_get = 6, manifest_chunk = 7.

- SchemaGet with an empty root doubles as discovery of which servers share a transport; replies are told apart by root.
- ManifestChunk data max_size in pn.options is chosen so pn_Message_size does not grow (at most 512 against the current 522). Chunks may split lines; the client concatenates until last. The transmitted manifest includes the header.
- Manifest request handling sits behind a compile option (name to pick, default on); a server built without it ignores ManifestGet.
- Regeneration: proto/generate.sh covers nanopb only; pn/python/src/pn/_proto.py was generated with betterproto. Add that step to the script.

## Extraction routes (README material)

1. Host schema tool (primary): a host executable that compiles the project's tree-building code with PN_SCHEMA_ONLY, links only pn, and writes to stdout. Zero firmware cost, a build artifact, and the input to the consistency checks. pn/example gets mixer_schema.c to demonstrate it. Cross-compiled projects need a second configure with the host compiler: a standalone tools/ CMake project or an ExternalProject.
2. Over the wire (development): ManifestGet. Ground truth from running firmware, and the only route when the tree depends on runtime state.
3. Debug sink (bring-up): the generator pointed at UART, RTT, or semihosting. Nothing to add to the library.

## Consistency checks (README material)

- Committed manifest vs build: a test regenerates via the host tool and diffs canonical forms. Comparing canonical forms permits hand annotation of the committed file.
- Committed manifest vs device: the hash in the committed header against the hash a running server reports. A mismatch means a stale build or a runtime-dependent tree.

## Phases

### Phase 0 — spec (done)

- pn/MANIFEST.md: format contract only, language-agnostic, soft-wrapped.

### Phase 1 — C generator and hash (done)

- pn_manifest.h/.c: pn_schema_validate(), pn_schema_result_name(), pn_schema_hash(), pn_manifest_write(); private pn_fnv.h/.c.
- pn_server_start() validates via pn_schema_hash() and caches the hash; pn_server_schema_hash(); pn_server_print_tree() removed.
- PN_FN and PN_SCHEMA_ONLY in pn_server.h; mixer.c uses them; pn/example/mixer_schema.c is the host schema tool.
- The amp tree lives in pn/test/amp_tree.c (shared by test_pn and the pn_amp_schema tool).
- Fixtures pn/test/fixtures/amp.manifest and mixer.manifest, checked by CTest through pn_add_manifest_test() and pn/cmake/check_manifest.cmake (canonical-form diff plus declared-hash check). Phase 4 reuses the helper for s9.
- README: component list, module pattern excerpt with PN_FN, Schema Manifest section (writer, schema-only builds, host schema tool, committed manifests).

### Phase 2 — wire (done)

- pn.proto: SchemaGet/Schema, ManifestGet/ManifestChunk; pn.options bounds roots at PN_MAX_PATH and the chunk payload at 512 so pn_Message_size stays 522 (asserted in pn_server.c). generate.sh regenerates C and Python; pn/make_venv.sh builds pn/venv with the [dev,codegen] extras.
- Server answers both requests. No compile option for the manifest request after all: the writer is already present for the hash, and streaming reuses the Update buffers, so an option would save nothing measurable.
- C client: pn_client_get_schema(), pn_client_get_manifest(), on_schema and on_manifest callbacks.
- Tests: round trips for both requests, root addressing (named, any, other), and a three-chunk manifest.
- README: Protocol message types, Schema Manifest "Over the wire".

### Phase 3 — Python (done)

- pn/python/src/pn/manifest.py: fnv1a32, canonicalize, schema_hash, validate, dump, parse, Entry, Manifest, SchemaError, ManifestError. Byte-wise canonicalization; validation mirrors C except depth, which is a C server limit.
- Server validates at creation (SchemaError), caches manifest and schema_hash, answers SchemaGet and ManifestGet (512-byte chunks).
- Client: get_schema/get_manifest with on_schema/on_manifest callbacks; request_schema/request_manifest coroutines; check_schema with on_mismatch warn/raise/ignore; SchemaMismatch, SchemaMismatchWarning.
- CLI pn-manifest: pull, hash, check (UDS only: pn has no serial transport).
- Tests: dump of the amp and mixer descriptor trees is byte-identical to the C-generated fixtures; parser and validation cases; wire tests including chunking and the coroutines.
- Docs: pn/python/README.md.

### Phase 4 — s9 (done)

- src/s9_params.h and src/s9_params.c: public s9_params_t, s9_params_build(p, void *osc), shims behind PN_SCHEMA_ONLY, PN_FN.
- src/s9_pn.c embeds s9_params_t, builds with the oscilloscope from the component registry, fails creation when the server refuses to start, logs the schema hash.
- tools/s9_schema.c and the s9_schema target (links only pn); s9.manifest at the repo root; test/CMakeLists.txt registers pn_add_manifest_test.
- python/s9sd: ranges from the manifest (path given on the command line, default the checkout's s9.manifest), check_schema on every connect with a warning, then the initial gets.
- Docs: s9 README "Parameters".

## Before merging to develop

- Delete this file.
- Decide whether the two s9sd egg-info files belong in git at all.

### Phase 5 — later

- A description field on nodes emitted as trailing comments, so documentation changes do not change the hash.
- An error reply for Set or Get on an unknown path.
- An offline UI generator driven by the manifest.
- A CMake helper for cross-compiled schema tool builds.
- Factor pn into its own repository.

## Documentation placement

| Content | Where | When |
|---|---|---|
| Format, canonical form, hash, tree requirements, example | pn/MANIFEST.md | phase 0 |
| README pointer and schema manifest section, PN_FN pattern, host tool recipe, extraction routes | pn/README.md | phase 1 |
| Generator and hash API contracts | pn_manifest.h, pn_server.h | phase 1 |
| Protocol messages, addressing, chunking | pn/README.md Protocol, pn.proto comments | phase 2 |
| Client API, mismatch policy default | pn_client.h, pn/python/README.md | phases 2–3 |
| Byte-wise canonicalization constraint | comment at the Python canonicalize function | phase 3 |
| Consistency checks recipe | pn/README.md | phase 4 |
| s9 tool, manifest, test, s9sd behavior | s9 README | phase 4 |

Rules applied throughout: no process references, no s9 vocabulary in pn, no constants duplicated into prose, docs land in the same commit as the behavior they describe, soft-wrapped markdown.
