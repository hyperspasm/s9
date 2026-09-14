"""Write packets.json: pn packets as the Python implementation frames them,
each with the message it carries, for other implementations to check their
encoders and decoders against.

    ../../venv/bin/python make_packets.py > packets.json
"""
import json
import sys

sys.path.insert(0, "../../python/src")
from pn import _codec, _proto as pb

MANIFEST = "# pn manifest v1 amp fnv1a32=fb9c41f2\n"

CASES = [
    ("set_float", pb.Message(set=pb.Set(path="amp/eq/bass", value=pb.Value(float_val=-3.5))),
     {"set": {"path": "amp/eq/bass", "value": {"float_val": -3.5}}}),
    ("set_float_inexact", pb.Message(set=pb.Set(path="amp/eq/bass", value=pb.Value(float_val=0.1))),
     {"set": {"path": "amp/eq/bass", "value": {"float_val": 0.1}}}),
    ("set_int", pb.Message(set=pb.Set(path="amp/volume", value=pb.Value(int_val=42))),
     {"set": {"path": "amp/volume", "value": {"int_val": 42}}}),
    ("set_int_zero", pb.Message(set=pb.Set(path="amp/volume", value=pb.Value(int_val=0))),
     {"set": {"path": "amp/volume", "value": {"int_val": 0}}}),
    ("set_int_negative", pb.Message(set=pb.Set(path="amp/volume", value=pb.Value(int_val=-7))),
     {"set": {"path": "amp/volume", "value": {"int_val": -7}}}),
    ("set_bool_true", pb.Message(set=pb.Set(path="amp/power", value=pb.Value(bool_val=True))),
     {"set": {"path": "amp/power", "value": {"bool_val": True}}}),
    ("set_bool_false", pb.Message(set=pb.Set(path="amp/power", value=pb.Value(bool_val=False))),
     {"set": {"path": "amp/power", "value": {"bool_val": False}}}),
    ("set_enum", pb.Message(set=pb.Set(path="amp/input", value=pb.Value(enum_val=2))),
     {"set": {"path": "amp/input", "value": {"enum_val": 2}}}),
    ("set_string", pb.Message(set=pb.Set(path="amp/name", value=pb.Value(string_val="µs"))),
     {"set": {"path": "amp/name", "value": {"string_val": "µs"}}}),
    ("action", pb.Message(set=pb.Set(path="amp/reset")),
     {"set": {"path": "amp/reset"}}),
    ("get", pb.Message(get=pb.Get(path="amp")),
     {"get": {"path": "amp"}}),
    ("update_float", pb.Message(update=pb.Update(path="amp/eq/bass", value=pb.Value(float_val=0.25))),
     {"update": {"path": "amp/eq/bass", "value": {"float_val": 0.25}}}),
    ("update_string", pb.Message(update=pb.Update(path="amp/name", value=pb.Value(string_val="vocals"))),
     {"update": {"path": "amp/name", "value": {"string_val": "vocals"}}}),
    ("schema_get_any", pb.Message(schema_get=pb.SchemaGet(root="")),
     {"schema_get": {"root": ""}}),
    ("schema_get", pb.Message(schema_get=pb.SchemaGet(root="amp")),
     {"schema_get": {"root": "amp"}}),
    ("schema", pb.Message(schema=pb.Schema(hash=0xFB9C41F2, root="amp")),
     {"schema": {"hash": 0xFB9C41F2, "root": "amp"}}),
    ("manifest_get", pb.Message(manifest_get=pb.ManifestGet(root="amp")),
     {"manifest_get": {"root": "amp"}}),
    ("manifest_chunk_last", pb.Message(manifest_chunk=pb.ManifestChunk(data=MANIFEST.encode(), last=True)),
     {"manifest_chunk": {"data": MANIFEST, "last": True}}),
    ("manifest_chunk", pb.Message(manifest_chunk=pb.ManifestChunk(data=b"abc", last=False)),
     {"manifest_chunk": {"data": "abc", "last": False}}),
]

json.dump([{"name": n, "frame": _codec.encode(m).hex(), "message": d} for n, m, d in CASES],
          sys.stdout, indent=1, ensure_ascii=False)
sys.stdout.write("\n")
