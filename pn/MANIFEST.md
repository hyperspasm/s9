# pn Manifest Format

Version 1.

## Purpose

A pn server's schema is the shape of its parameter tree: the name, type, and constraints of every node. The schema is defined in code, and clients know it in advance. The manifest is a canonical text rendering of a schema. It serves three purposes:

- a human-readable, diffable declaration of a schema that can be committed alongside the code that defines it
- the input to the schema hash, a fingerprint that two parties can compare to detect that they disagree about a schema
- a machine-readable description of a schema for tooling such as test fixtures and generated user interfaces

The manifest describes the schema only. It carries no default values, no current values, and no information about how nodes are bound to application state.

This document specifies the format, the canonical form, and the hash. It is independent of any implementation language.

## Terms

- **Schema** — a parameter tree as declared: names, types, constraints, and the order of children.
- **Manifest** — the text rendering of a schema specified here.
- **Entry** — one line of a manifest describing one leaf node.
- **Canonical form** — a manifest with comments, blank lines, and whitespace variation removed; the input to the hash.
- **Schema hash** — the FNV-1a 32-bit hash of the canonical form.
- **Declared hash** — the schema hash as written in a manifest's header.
- **Generator** — an implementation that produces a manifest from a schema.
- **Consumer** — an implementation that reads a manifest.

## File format

### Encoding

A manifest is a sequence of lines terminated by LF (0x0A). Content is UTF-8 without a byte-order mark. Consumers must tolerate CRLF line endings; the CR is whitespace and is removed by canonicalization.

All structure in the format (line breaks, field separators, the comment introducer, and the path and label separators) is expressed in ASCII bytes. UTF-8 guarantees those byte values never occur inside a multibyte sequence, so the format can be processed byte-wise without decoding.

Each line is a comment line, a blank line, or an entry.

### Comments

`#` begins a comment that extends to the end of the line. A comment may follow an entry on the same line. Because `#` cannot appear in a name or label, it is unambiguous anywhere on a line.

Comments and blank lines carry no information: consumers ignore them and they are excluded from the hash.

### Header

A generator emits a header as the first line:

```
# pn manifest v1 <root> fnv1a32=<hash>
```

- `v1` — the manifest format version
- `<root>` — the root node name
- `<hash>` — the schema hash as exactly 8 lowercase hex digits

Fields are separated by single spaces. The header is a comment, so it is excluded from the hash by construction.

A consumer takes the declared hash from the header. It may verify the declared hash against the body and should treat a mismatch as a malformed manifest. A manifest without a header is readable but has no declared hash.

### Entries

One entry per leaf node, in tree order (see Ordering). Groups produce no entries; the tree structure is implied by the paths.

```
entry  = path SP type [":" unit] [SP meta] [SP "ro"]

path   = name *("/" name)          ; first name is the root, last is the leaf
type   = "action" | "bool" | "int" | "float" | "enum" | "string"
unit   = token                     ; int and float only; see Units

meta for int    = int32 SP int32 SP int32      ; min max step
meta for float  = f32 SP f32 SP f32            ; min max step
meta for enum   = label *("|" label)           ; one or more labels, in index order
meta for string = uint32                       ; max_len
meta for action = (none)
meta for bool   = (none)
```

Fields are separated by exactly one space (SP), with no leading or trailing whitespace.

The `ro` marker declares a read-only node: the server reports its value and pushes changes, but refuses to set it. An action cannot be read-only. The marker is part of the entry, so it is part of the hash: a node becoming read-only is a change of schema.

### Value encodings

- **int32** — decimal, with a leading `-` for negative values; no `+`, no leading zeros. Zero is written `0`. Examples: `-1`, `0`, `31`.
- **uint32** — decimal, no sign, no leading zeros.
- **f32** — `0x` followed by exactly 8 lowercase hex digits: the IEEE-754 binary32 bit pattern, most significant byte first. Examples: `0x3f800000` is 1.0, `0xbf800000` is -1.0, `0x00000000` is 0.0, `0x80000000` is -0.0. A generator emits the bit pattern verbatim, so the sign of zero and any NaN payload are preserved. A generator whose native representation is wider than binary32 rounds to nearest-even before encoding. Floats are never written in decimal: decimal rendering of binary floats is not reproducible across implementations, and it requires float formatting support that small targets lack.
- **label** — an enum label, subject to Name rules.

The format assigns no meaning to `step`; it is recorded as declared.

### Units

An int or float entry may declare a unit: what the value is measured in, written after the type with a colon between them, as in `int:cents` or `float:Hz`. A unit is subject to the Name rules and must not contain `:`. Other types carry no unit. The unit is part of the entry, so it is part of the hash: a value changing its unit is a change of schema.

The format assigns no meaning to a unit; it is recorded as declared, and a consumer that does not know a unit still knows the value's type and range. Identity is byte equality, so `Hz` and `hz` are different units. Any unit is allowed; these spellings are recommended, so that the common ones are written one way:

| unit | for |
|---|---|
| `Hz` | frequency |
| `dB` | level or gain in decibels; what it is relative to is the schema's business |
| `s`, `ms` | time |
| `cents`, `semitones` | pitch intervals |
| `%` | a percentage |
| `samples` | a count of sample frames |

### Ordering

Entries appear in pre-order depth-first traversal of the tree from the root, visiting the children of each group in declared order (the order in which the schema lists them). Only leaves produce entries.

Consequences:

- Declared order is significant. Reordering siblings changes the manifest and the hash.
- Empty groups are invisible. Two schemas that differ only by an empty group have the same manifest.

### Name rules

Node names (including the root) and enum labels are non-empty UTF-8 strings. They must not contain bytes 0x00–0x20 or 0x7F (control characters and ASCII whitespace), or the characters:

- `/` — path separator
- `#` — comment introducer
- `|` — label separator

Bytes 0x80 and above must form valid UTF-8 sequences.

Names and labels are opaque byte strings. Identity is byte equality: no Unicode normalization is applied, and two encodings of the same character are different names. Any length limit an implementation imposes is measured in bytes.

## Canonical form and schema hash

### Canonicalization

Canonical form is derived from manifest text as follows. Whitespace means space (0x20), tab (0x09), and CR (0x0D); no other byte is whitespace.

1. Split the text into lines on LF.
2. In each line, remove everything from the first `#` to the end of the line.
3. Trim leading and trailing whitespace.
4. If the line is now empty, drop it.
5. Replace each run of whitespace with a single space.
6. Append LF.
7. Concatenate the lines in order.

A generator emits entries that are already canonical, so the bytes it writes are the bytes it hashes. A consumer canonicalizes before hashing or comparing, so a hand-edited manifest may carry comments and loose whitespace without affecting its hash.

### Hash

The schema hash is FNV-1a, 32-bit, over the bytes of the canonical form:

```
hash = 0x811c9dc5                       ; offset basis
for each byte b:
    hash = hash XOR b
    hash = (hash * 0x01000193) mod 2^32 ; FNV prime
```

Test vectors:

| input      | hash       |
|------------|------------|
| `""`       | `811c9dc5` |
| `"a"`      | `e40c292c` |
| `"foobar"` | `bf9cf968` |

A schema with no leaves has an empty canonical form and hashes to `811c9dc5`.

The hash is an exact-match check. Any change to a name, type, unit, constraint, label, or order changes it. Equal hashes mean the same schema; unequal hashes say nothing about how the schemas differ.

## Tree requirements

A schema has a manifest only if:

- every name and label satisfies Name rules
- sibling names are unique
- every enum has at least one label

## Example

The schema below has root `amp` and covers every type. Indentation shows grouping, and the listing is in declared order.

```
amp
  power    bool
  volume   int     min 0, max 100, step 1, in %
  eq
    bass   float   min -12.0, max 12.0, step 0.5, in dB
    treble float   min -12.0, max 12.0, step 0.5, in dB
  input    enum    mic, line, aux
  name     string  max_len 32
  level    float   min 0.0, max 1.0, step 0.0, read-only
  reset    action
```

Its manifest as a generator emits it:

```
# pn manifest v1 amp fnv1a32=76a2b061
amp/power bool
amp/volume int:% 0 100 1
amp/eq/bass float:dB 0xc1400000 0x41400000 0x3f000000
amp/eq/treble float:dB 0xc1400000 0x41400000 0x3f000000
amp/input enum mic|line|aux
amp/name string 32
amp/level float 0x00000000 0x3f800000 0x00000000 ro
amp/reset action
```

The `eq` group produces no entry of its own. Its leaves appear at its position in declared order, before the siblings that follow it.

The same manifest annotated by hand, with aligned columns, has the same canonical form and therefore the same hash:

```
# pn manifest v1 amp fnv1a32=76a2b061

# Front panel
amp/power     bool
amp/volume    int:%     0 100 1                              # the knob

# Tone
amp/eq/bass   float:dB  0xc1400000 0x41400000 0x3f000000    # -12 .. 12
amp/eq/treble float:dB  0xc1400000 0x41400000 0x3f000000
amp/input     enum      mic|line|aux
amp/name      string    32
amp/level     float     0x00000000 0x3f800000 0x00000000 ro # a meter
amp/reset     action
```
