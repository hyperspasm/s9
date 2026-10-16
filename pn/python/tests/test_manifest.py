"""Manifest tests: the Python generator against the C-generated fixtures, the
parser, canonicalization, the hash, and schema validation."""
from pathlib import Path

import pytest

import pn
from pn import manifest
from pn._types import ParamType

FIXTURES = Path(__file__).resolve().parents[2] / "test" / "fixtures"


# --- Example schemas, matching the C trees the fixtures were generated from ---

class AmpEq:
    bass = pn.Float(min=-12.0, max=12.0, step=0.5, unit="dB")
    treble = pn.Float(min=-12.0, max=12.0, step=0.5, unit="dB")


class Amp:
    """The example schema from MANIFEST.md."""
    power = pn.Bool()
    volume = pn.Int(min=0, max=100, step=1, unit="%")
    eq = pn.Group(AmpEq)
    input = pn.Enum(labels=["mic", "line", "aux"])
    name = pn.String(max_len=32)
    level = pn.Float(min=0.0, max=1.0, step=0.0, readonly=True)

    @pn.Action
    def reset(self):
        pass


AMP_HASH = 0x76A2B061


class MixerEq:
    enabled = pn.Bool()
    bass = pn.Float(min=-12.0, max=12.0, step=0.1, unit="dB")
    mid = pn.Float(min=-12.0, max=12.0, step=0.1, unit="dB")
    treble = pn.Float(min=-12.0, max=12.0, step=0.1, unit="dB")


class MixerChannel:
    gain = pn.Float(min=-72.0, max=6.0, step=0.1, unit="dB")
    pan = pn.Float(min=-1.0, max=1.0, step=0.01)
    input = pn.Enum(labels=["mic", "line"])
    eq = pn.Group(MixerEq)


class Mixer:
    """The example mixer: eight channels added as instance attributes."""
    master_gain = pn.Float(min=-72.0, max=6.0, step=0.1, unit="dB")
    master_mute = pn.Bool()

    def __init__(self):
        for i in range(8):
            setattr(self, f"ch{i + 1}", MixerChannel())


# --- Hash ---

def test_fnv1a32_vectors():
    assert manifest.fnv1a32(b"") == 0x811C9DC5
    assert manifest.fnv1a32(b"a") == 0xE40C292C
    assert manifest.fnv1a32(b"foobar") == 0xBF9CF968


# --- Canonical form ---

def test_canonicalize_removes_comments_blank_lines_and_whitespace():
    text = "# header\r\n\r\n  a/b\tint   1 2 3   # trailing\r\n\n a/c bool \n"
    assert manifest.canonicalize(text) == b"a/b int 1 2 3\na/c bool\n"


def test_canonicalize_empty():
    assert manifest.canonicalize("") == b""
    assert manifest.canonicalize("# only a comment\n\n") == b""


def test_canonicalize_only_ascii_whitespace_separates_fields():
    # U+00A0 is whitespace to str.split() but not to the format
    line = "r/a b bool\n"
    assert manifest.canonicalize(line) == line.encode("utf-8")


def test_schema_hash_ignores_comments_and_whitespace():
    plain = "r/a bool\nr/b int 0 1 1\n"
    annotated = "# pn manifest v1 r fnv1a32=00000000\n\n# x\nr/a   bool  # a\r\nr/b\tint 0 1 1\r\n"
    assert manifest.schema_hash(annotated) == manifest.schema_hash(plain)


# --- Dump against the C-generated fixtures ---

def test_dump_amp_matches_fixture():
    expected = (FIXTURES / "amp.manifest").read_text(encoding="utf-8")
    assert manifest.dump(Amp(), "amp") == expected
    assert manifest.schema_hash(expected) == AMP_HASH


def test_dump_mixer_matches_fixture():
    expected = (FIXTURES / "mixer.manifest").read_text(encoding="utf-8")
    assert manifest.dump(Mixer(), "mixer") == expected


def test_dump_header_hash_is_body_hash():
    text = manifest.dump(Amp(), "amp")
    header, body = text.split("\n", 1)
    assert header == f"# pn manifest v1 amp fnv1a32={manifest.fnv1a32(body.encode()):08x}"


def test_dump_float_bits():
    class T:
        f = pn.Float(min=-0.0, max=0.1, step=0.0)
    assert manifest.dump(T(), "r").splitlines()[1] == "r/f float 0x80000000 0x3dcccccd 0x00000000"


def test_dump_int_range():
    class T:
        i = pn.Int(min=-2**31, max=2**31 - 1, step=-1)
    assert manifest.dump(T(), "r").splitlines()[1] == "r/i int -2147483648 2147483647 -1"


def test_dump_utf8_labels():
    class T:
        u = pn.Enum(labels=["µs", "ms"])
    assert manifest.dump(T(), "r").splitlines()[1] == "r/u enum µs|ms"


def test_dump_no_leaves():
    class T:
        pass
    assert manifest.dump(T(), "r") == "# pn manifest v1 r fnv1a32=811c9dc5\n"


# --- Parse ---

def test_parse_fixture():
    text = (FIXTURES / "amp.manifest").read_bytes()
    m = manifest.parse(text)
    assert m.root == "amp"
    assert m.declared_hash == AMP_HASH
    assert m.schema_hash == AMP_HASH
    assert [e.path for e in m.entries] == [
        "amp/power", "amp/volume", "amp/eq/bass", "amp/eq/treble",
        "amp/input", "amp/name", "amp/level", "amp/reset",
    ]
    by = m.by_path
    assert by["amp/power"].type == ParamType.BOOL
    assert (by["amp/volume"].min, by["amp/volume"].max, by["amp/volume"].step) == (0, 100, 1)
    assert (by["amp/eq/bass"].min, by["amp/eq/bass"].max, by["amp/eq/bass"].step) == (-12.0, 12.0, 0.5)
    assert by["amp/input"].labels == ["mic", "line", "aux"]
    assert by["amp/name"].max_len == 32
    assert by["amp/reset"].type == ParamType.ACTION
    assert by["amp/eq/bass"].name == "bass"
    assert by["amp/level"].readonly and not by["amp/volume"].readonly
    assert (by["amp/volume"].unit, by["amp/eq/bass"].unit, by["amp/level"].unit) == ("%", "dB", None)


def test_unit_is_written_and_read_back():
    class R:
        f = pn.Float(min=0.0, max=1.0, step=0.0, unit="Hz")
        i = pn.Int(min=0, max=1, step=1, unit="cents", readonly=True)
        plain = pn.Int(min=0, max=1, step=1)

    text = manifest.dump(R(), "r")
    assert text.split("\n")[1:4] == [
        "r/f float:Hz 0x00000000 0x3f800000 0x00000000",
        "r/i int:cents 0 1 1 ro",
        "r/plain int 0 1 1",
    ]
    m = manifest.parse(text)
    assert [e.unit for e in m.entries] == ["Hz", "cents", None]
    assert m.schema_hash == m.declared_hash


def test_unit_changes_the_hash():
    class A:
        f = pn.Float(min=0.0, max=1.0, step=0.0, unit="Hz")

    class B:
        f = pn.Float(min=0.0, max=1.0, step=0.0, unit="kHz")

    class C:
        f = pn.Float(min=0.0, max=1.0, step=0.0)

    hashes = {manifest.schema_hash(manifest.dump(cls(), "r")) for cls in (A, B, C)}
    assert len(hashes) == 3


def test_readonly_is_marked_and_read_back():
    class R:
        meter = pn.Float(min=0.0, max=1.0, step=0.0, readonly=True)
        gain = pn.Float(min=0.0, max=1.0, step=0.0)
        on = pn.Bool(readonly=True)

    text = manifest.dump(R(), "r")
    assert text.split("\n")[1:4] == [
        "r/meter float 0x00000000 0x3f800000 0x00000000 ro",
        "r/gain float 0x00000000 0x3f800000 0x00000000",
        "r/on bool ro",
    ]
    m = manifest.parse(text)
    assert [e.readonly for e in m.entries] == [True, False, True]
    assert m.schema_hash == m.declared_hash


def test_readonly_action_is_rejected():
    with pytest.raises(manifest.ManifestError):
        manifest.parse("r/go action ro\n")


def test_parse_round_trips_through_dump():
    text = manifest.dump(Mixer(), "mixer")
    m = manifest.parse(text)
    assert "".join(e.line() + "\n" for e in m.entries) == text.split("\n", 1)[1]


def test_parse_annotated_text():
    text = (
        "# pn manifest v1 amp fnv1a32=76a2b061\r\n"
        "\r\n"
        "# Front panel\r\n"
        "amp/power\tbool\r\n"
        "amp/volume   int:% 0 100 1   # percent\r\n"
        "amp/eq/bass  float:dB 0xc1400000 0x41400000 0x3f000000\r\n"
        "amp/eq/treble float:dB 0xc1400000 0x41400000 0x3f000000\r\n"
        "amp/input enum mic|line|aux\r\n"
        "amp/name string 32\r\n"
        "amp/level  float 0x00000000 0x3f800000 0x00000000  ro   # a meter\r\n"
        "amp/reset action\r\n"
    )
    m = manifest.parse(text)
    assert m.declared_hash == AMP_HASH
    assert m.schema_hash == AMP_HASH
    assert len(m.entries) == 8


def test_parse_without_header():
    m = manifest.parse("r/a bool\nr/b action\n")
    assert m.root == "r"
    assert m.declared_hash is None
    assert m.schema_hash == manifest.fnv1a32(b"r/a bool\nr/b action\n")


@pytest.mark.parametrize("text", [
    "r/a nonsense\n",                       # unknown type
    "r/a\n",                                # no type
    "r/a bool 1\n",                         # constraint on a bool
    "r/a int 0 1\n",                        # missing step
    "r/a int 0 1 x\n",                      # not an integer
    "r/a int 0 2147483648 1\n",             # out of int32 range
    "r/a int 00 1 1\n",                     # leading zero
    "r/a float 0x3f800000 1.0 0x0\n",       # not a bit pattern
    "r/a float 0x3F800000 0x3f800000 0x00000000\n",  # uppercase hex
    "r/a enum\n",                           # no labels
    "r/a enum mic|li/ne\n",                 # slash in a label
    "r/a string -1\n",                      # negative max_len
    "r/a bool:dB\n",                        # a unit on a bool
    "r/a enum:dB mic\n",                    # a unit on an enum
    "r/a int: 0 1 1\n",                     # an empty unit
    "r/a int:a:b 0 1 1\n",                  # a colon in a unit
    "r/a bool\ns/b bool\n",                 # two roots
    "# pn manifest v1 s fnv1a32=00000000\nr/a bool\n",  # header root mismatch
    "\n",                                   # nothing at all
])
def test_parse_rejects(text):
    with pytest.raises(manifest.ManifestError):
        manifest.parse(text)


def test_parse_rejects_invalid_utf8():
    with pytest.raises(manifest.ManifestError):
        manifest.parse(b"r/\xff bool\n")


# --- Validation ---

def test_validate_ok():
    manifest.validate(Amp(), "amp")


@pytest.mark.parametrize("unit", ["", "a:b", "per cent", "d#B"])
def test_validate_bad_unit(unit):
    class R:
        f = pn.Float(min=0.0, max=1.0, step=0.0, unit=unit)

    with pytest.raises(manifest.SchemaError):
        manifest.validate(R(), "r")


@pytest.mark.parametrize("name", ["", "my root", "a/b", "a#b", "a|b", "a\tb", "\x7f"])
def test_validate_bad_root_name(name):
    with pytest.raises(manifest.SchemaError):
        manifest.validate(Amp(), name)


def test_validate_bad_label():
    class T:
        e = pn.Enum(labels=["mic", "line in"])
    with pytest.raises(manifest.SchemaError):
        manifest.validate(T(), "r")


def test_validate_empty_enum():
    class T:
        e = pn.Enum(labels=[])
    with pytest.raises(manifest.SchemaError):
        manifest.validate(T(), "r")


def test_validate_int32_range():
    class T:
        i = pn.Int(min=0, max=2**31, step=1)
    with pytest.raises(manifest.SchemaError):
        manifest.validate(T(), "r")


def test_validate_float_range():
    class T:
        f = pn.Float(min=0.0, max=1e39, step=0.0)
    with pytest.raises(manifest.SchemaError):
        manifest.validate(T(), "r")


def test_validate_path_too_long():
    class Leaf:
        z = pn.Bool()
    # Children added as instance attributes nest arbitrarily deep
    mid = Leaf()
    setattr(mid, "y" * 100, Leaf())
    root = Leaf()
    setattr(root, "x" * 200, mid)
    # r/x…x/y…y/z is 305 bytes
    with pytest.raises(manifest.SchemaError):
        manifest.validate(root, "r")


def test_validate_path_fits():
    class Leaf:
        z = pn.Bool()
    t = Leaf()
    manifest.validate(t, "r" * 253)  # "r"*253 + "/z" is 255 bytes
    with pytest.raises(manifest.SchemaError):
        manifest.validate(t, "r" * 254)


def test_server_rejects_invalid_schema():
    class T:
        e = pn.Enum(labels=[])
    with pytest.raises(pn.SchemaError):
        pn.Server(T(), name="r")


def test_server_manifest_and_hash():
    server = pn.Server(Amp(), name="amp")
    assert server.manifest == (FIXTURES / "amp.manifest").read_text(encoding="utf-8")
    assert server.schema_hash == AMP_HASH
