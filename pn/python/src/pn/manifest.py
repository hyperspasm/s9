"""The schema manifest: a canonical text rendering of a parameter tree and the
schema hash computed from it. The format is specified in MANIFEST.md.

A manifest is produced from a tree of descriptors (dump) or read from text
(parse). Text is handled as UTF-8 bytes throughout, so that only ASCII
whitespace and the ASCII structure characters are significant.
"""
from __future__ import annotations

import re
import struct
from dataclasses import dataclass
from typing import Optional

from ._types import ParamType
from ._tree import _Param, Action, Group, _iter_params

__all__ = [
    "VERSION", "MAX_PATH",
    "SchemaError", "ManifestError", "Entry", "Manifest",
    "fnv1a32", "canonicalize", "schema_hash", "validate", "dump", "parse",
]

VERSION = "v1"

# Longest path the wire carries, in bytes: the path field holds 256 with its terminator.
MAX_PATH = 255

_FNV_OFFSET = 0x811C9DC5
_FNV_PRIME = 0x01000193

_TYPE_TOKEN = {
    ParamType.ACTION: "action",
    ParamType.BOOL: "bool",
    ParamType.INT: "int",
    ParamType.FLOAT: "float",
    ParamType.ENUM: "enum",
    ParamType.STRING: "string",
}
_TOKEN_TYPE = {token: t for t, token in _TYPE_TOKEN.items()}

_HEADER = re.compile(rb"# pn manifest v1 (\S+) fnv1a32=([0-9a-f]{8})[ \t\r]*$")
_WHITESPACE = re.compile(rb"[ \t\r]+")
_INT32 = re.compile(r"-?(0|[1-9][0-9]*)$")
_UINT32 = re.compile(r"(0|[1-9][0-9]*)$")
_F32 = re.compile(r"0x[0-9a-f]{8}$")


class SchemaError(ValueError):
    """The tree cannot be represented as a manifest."""


class ManifestError(ValueError):
    """The text is not a well-formed manifest."""


# --- Hash and canonical form ---

def fnv1a32(data: bytes) -> int:
    h = _FNV_OFFSET
    for b in data:
        h = ((h ^ b) * _FNV_PRIME) & 0xFFFFFFFF
    return h


def _as_bytes(text) -> bytes:
    return text.encode("utf-8") if isinstance(text, str) else bytes(text)


def canonicalize(text) -> bytes:
    """Canonical form of manifest text: comments and blank lines removed, whitespace collapsed."""
    lines = []
    for line in _as_bytes(text).split(b"\n"):
        line = line.split(b"#", 1)[0]
        # Only space, tab, and CR separate fields. This must stay byte-wise:
        # str.split() treats Unicode whitespace as whitespace, which would
        # split names the format allows.
        fields = [f for f in _WHITESPACE.split(line) if f]
        if fields:
            lines.append(b" ".join(fields))
    return b"\n".join(lines) + b"\n" if lines else b""


def schema_hash(text) -> int:
    """The schema hash of manifest text."""
    return fnv1a32(canonicalize(text))


# --- Entries ---

@dataclass
class Entry:
    """One leaf of a manifest. Which constraint fields apply depends on type."""
    path: str
    type: ParamType
    min: Optional[object] = None    # int and float
    max: Optional[object] = None    # int and float
    step: Optional[object] = None   # int and float
    labels: Optional[list] = None   # enum
    max_len: Optional[int] = None   # string
    readonly: bool = False          # any type but action
    unit: Optional[str] = None      # int and float

    @property
    def name(self) -> str:
        return self.path.rsplit("/", 1)[-1]

    def line(self) -> str:
        """The entry as it appears in a manifest."""
        fields = [self.path, _TYPE_TOKEN[self.type] + (":" + self.unit if self.unit else "")]
        if self.type == ParamType.INT:
            fields += [str(self.min), str(self.max), str(self.step)]
        elif self.type == ParamType.FLOAT:
            fields += [_f32(self.min), _f32(self.max), _f32(self.step)]
        elif self.type == ParamType.ENUM:
            fields.append("|".join(self.labels))
        elif self.type == ParamType.STRING:
            fields.append(str(self.max_len))
        if self.readonly:
            fields.append("ro")
        return " ".join(fields)


@dataclass
class Manifest:
    """A parsed manifest."""
    root: str
    entries: list
    declared_hash: Optional[int]  # from the header, or None without one
    canonical: bytes

    @property
    def schema_hash(self) -> int:
        return fnv1a32(self.canonical)

    @property
    def by_path(self) -> dict:
        return {e.path: e for e in self.entries}


def _f32(value) -> str:
    return "0x%08x" % struct.unpack(">I", struct.pack(">f", value))[0]


def _name_problem(name) -> Optional[str]:
    """Why a name or label violates the name rules, or None if it does not."""
    if not isinstance(name, str):
        return "is not a string"
    try:
        data = name.encode("utf-8")
    except UnicodeEncodeError:
        return "is not valid Unicode"
    if not data:
        return "is empty"
    for b in data:
        if b <= 0x20 or b == 0x7F or b in b"/#|":
            return "contains a forbidden character"
    return None


# --- Dump ---

def _check_name(name, what: str, where: str) -> None:
    problem = _name_problem(name)
    if problem:
        raise SchemaError(f"{where}: {what} {name!r} {problem}")


def _unit_problem(unit) -> Optional[str]:
    """Why a unit is not one, or None: the name rules, and no colon, which separates it from the type."""
    problem = _name_problem(unit)
    if problem is None and ":" in unit:
        problem = "contains a colon"
    return problem


def _check_unit(unit, where: str) -> Optional[str]:
    if unit is None:
        return None
    problem = _unit_problem(unit)
    if problem:
        raise SchemaError(f"{where}: unit {unit!r} {problem}")
    return unit


def _entry(path: str, attr) -> Entry:
    t = attr.param_type
    ro = bool(getattr(attr, "readonly", False))
    if t == ParamType.INT:
        for v, what in ((attr.min, "min"), (attr.max, "max"), (attr.step, "step")):
            if not isinstance(v, int) or not -2**31 <= v < 2**31:
                raise SchemaError(f"{path}: {what} {v!r} is not an int32")
        return Entry(path, t, min=attr.min, max=attr.max, step=attr.step, readonly=ro,
                     unit=_check_unit(getattr(attr, "unit", None), path))
    if t == ParamType.FLOAT:
        for v, what in ((attr.min, "min"), (attr.max, "max"), (attr.step, "step")):
            try:
                struct.pack(">f", v)
            except (OverflowError, struct.error, TypeError):
                raise SchemaError(f"{path}: {what} {v!r} is not a binary32 float") from None
        return Entry(path, t, min=float(attr.min), max=float(attr.max), step=float(attr.step), readonly=ro,
                     unit=_check_unit(getattr(attr, "unit", None), path))
    if t == ParamType.ENUM:
        if not attr.labels:
            raise SchemaError(f"{path}: enum has no labels")
        for label in attr.labels:
            _check_name(label, "label", path)
        return Entry(path, t, labels=list(attr.labels), readonly=ro)
    if t == ParamType.STRING:
        if not isinstance(attr.max_len, int) or not 0 <= attr.max_len < 2**32:
            raise SchemaError(f"{path}: max_len {attr.max_len!r} is not a uint32")
        return Entry(path, t, max_len=attr.max_len, readonly=ro)
    return Entry(path, t, readonly=ro)


def _entries(obj, path: list):
    """Yield the entries under obj in declared order, validating as it goes."""
    for name, attr in _iter_params(obj):
        child_path = path + [name]
        path_str = "/".join(child_path)
        _check_name(name, "name", path_str)
        if len(path_str.encode("utf-8")) > MAX_PATH:
            raise SchemaError(f"{path_str}: path is longer than {MAX_PATH} bytes")
        if isinstance(attr, Group):
            yield from _entries(getattr(obj, name), child_path)
        elif isinstance(attr, (_Param, Action)):
            yield _entry(path_str, attr)
        else:
            # A child object added to the instance rather than declared on the class
            yield from _entries(attr, child_path)


def _walk(root, name: str):
    _check_name(name, "root name", name)
    if len(name.encode("utf-8")) > MAX_PATH:
        raise SchemaError(f"{name}: path is longer than {MAX_PATH} bytes")
    return _entries(root, [name])


def validate(root, name: str) -> None:
    """Raise SchemaError if the tree rooted at root, named name, has no manifest."""
    for _ in _walk(root, name):
        pass


def dump(root, name: str) -> str:
    """The manifest of a tree, as a generator emits it: header line and entries."""
    body = "".join(entry.line() + "\n" for entry in _walk(root, name))
    h = fnv1a32(body.encode("utf-8"))
    return f"# pn manifest {VERSION} {name} fnv1a32={h:08x}\n{body}"


# --- Parse ---

def _parse_int32(field: str, where: str) -> int:
    if not _INT32.match(field):
        raise ManifestError(f"{where}: {field!r} is not an integer")
    value = int(field)
    if not -2**31 <= value < 2**31:
        raise ManifestError(f"{where}: {field!r} is out of the int32 range")
    return value


def _parse_uint32(field: str, where: str) -> int:
    if not _UINT32.match(field):
        raise ManifestError(f"{where}: {field!r} is not an unsigned integer")
    value = int(field)
    if value >= 2**32:
        raise ManifestError(f"{where}: {field!r} is out of the uint32 range")
    return value


def _parse_f32(field: str, where: str) -> float:
    if not _F32.match(field):
        raise ManifestError(f"{where}: {field!r} is not a binary32 bit pattern")
    return struct.unpack(">f", bytes.fromhex(field[2:]))[0]


def _parse_entry(line: bytes) -> Entry:
    try:
        fields = line.decode("utf-8").split(" ")
    except UnicodeDecodeError as e:
        raise ManifestError(f"{line!r}: invalid UTF-8") from e
    if len(fields) < 2:
        raise ManifestError(f"{fields[0]!r}: expected a path and a type")
    path, typed, meta = fields[0], fields[1], fields[2:]
    token, colon, unit = typed.partition(":")
    unit = unit if colon else None
    readonly = bool(meta) and meta[-1] == "ro"
    if readonly:
        meta = meta[:-1]

    for segment in path.split("/"):
        problem = _name_problem(segment)
        if problem:
            raise ManifestError(f"{path}: name {segment!r} {problem}")

    t = _TOKEN_TYPE.get(token)
    if t is None:
        raise ManifestError(f"{path}: unknown type {token!r}")
    if readonly and t == ParamType.ACTION:
        raise ManifestError(f"{path}: an action cannot be read-only")
    if unit is not None:
        if t not in (ParamType.INT, ParamType.FLOAT):
            raise ManifestError(f"{path}: a {token} has no unit")
        problem = _unit_problem(unit)
        if problem:
            raise ManifestError(f"{path}: unit {unit!r} {problem}")

    def expect(n: int) -> None:
        if len(meta) != n:
            raise ManifestError(f"{path}: {token} takes {n} constraint fields, got {len(meta)}")

    if t == ParamType.INT:
        expect(3)
        lo, hi, step = (_parse_int32(f, path) for f in meta)
        return Entry(path, t, min=lo, max=hi, step=step, readonly=readonly, unit=unit)
    if t == ParamType.FLOAT:
        expect(3)
        lo, hi, step = (_parse_f32(f, path) for f in meta)
        return Entry(path, t, min=lo, max=hi, step=step, readonly=readonly, unit=unit)
    if t == ParamType.ENUM:
        expect(1)
        labels = meta[0].split("|")
        for label in labels:
            problem = _name_problem(label)
            if problem:
                raise ManifestError(f"{path}: label {label!r} {problem}")
        return Entry(path, t, labels=labels, readonly=readonly)
    if t == ParamType.STRING:
        expect(1)
        return Entry(path, t, max_len=_parse_uint32(meta[0], path), readonly=readonly)
    expect(0)
    return Entry(path, t, readonly=readonly)


def parse(text) -> Manifest:
    """Read a manifest. The header, if present, must be the first line."""
    data = _as_bytes(text)

    header = _HEADER.match(data.split(b"\n", 1)[0])
    header_root = declared = None
    if header:
        try:
            header_root = header.group(1).decode("utf-8")
        except UnicodeDecodeError as e:
            raise ManifestError("header root is not valid UTF-8") from e
        declared = int(header.group(2), 16)

    canonical = canonicalize(data)
    entries = [_parse_entry(line) for line in canonical.split(b"\n") if line]

    roots = {e.path.split("/", 1)[0] for e in entries}
    if len(roots) > 1:
        raise ManifestError(f"entries under more than one root: {sorted(roots)}")
    if header_root is not None and roots and roots != {header_root}:
        raise ManifestError(f"header root {header_root!r} does not match the entries")
    root = header_root if header_root is not None else next(iter(roots), None)
    if root is None:
        raise ManifestError("no header and no entries")

    return Manifest(root=root, entries=entries, declared_hash=declared, canonical=canonical)
