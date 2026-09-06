import betterproto

from ._types import ParamType
from ._tree import _Param, Action, Group, _iter_params, _bind_tree, find_param, walk_tree
from . import _proto as pb
from . import _codec
from . import manifest as _manifest

# Largest ManifestChunk payload: ManifestChunk.data max_size in pn.options
_MANIFEST_CHUNK = 512


def _extract_value(value):
    """Extract (python_value, ParamType) from a betterproto Value."""
    name, val = betterproto.which_one_of(value, "kind")
    type_map = {
        "bool_val": ParamType.BOOL,
        "int_val": ParamType.INT,
        "float_val": ParamType.FLOAT,
        "enum_val": ParamType.ENUM,
        "string_val": ParamType.STRING,
    }
    return val, type_map.get(name)


def _make_update(path, value, param_type):
    """Build a pb.Message containing an Update."""
    v = pb.Value()
    if param_type == ParamType.BOOL:
        v.bool_val = value
    elif param_type == ParamType.INT:
        v.int_val = int(value)
    elif param_type == ParamType.FLOAT:
        v.float_val = float(value)
    elif param_type == ParamType.ENUM:
        v.enum_val = int(value)
    elif param_type == ParamType.STRING:
        v.string_val = str(value)
    return pb.Message(update=pb.Update(path=path, value=v))


def _coerce(value, from_type, to_type):
    """Apply type coercion rules matching the C implementation.

    Returns coerced value, or None if coercion is not supported.
    """
    if from_type == to_type:
        return value
    if from_type == ParamType.INT and to_type == ParamType.FLOAT:
        return float(value)
    if from_type == ParamType.FLOAT and to_type == ParamType.INT:
        return int(value)
    if from_type == ParamType.INT and to_type == ParamType.BOOL:
        return value != 0
    if from_type == ParamType.INT and to_type == ParamType.ENUM:
        return int(value)
    return None


class Server:
    def __init__(self, root, *, name):
        """Serve the tree rooted at root under the given root name.

        The schema is fixed here: the tree is validated (SchemaError if it has
        no manifest), and its manifest and schema hash are computed once.
        """
        self.manifest = _manifest.dump(root, name)
        self.schema_hash = _manifest.schema_hash(self.manifest)
        self._root = root
        self._name = name
        self._send = None
        _bind_tree(root, name, self)

    def _dispatch_message(self, msg):
        """Dispatch a decoded protobuf Message. Returns list of response Messages."""
        name, payload = betterproto.which_one_of(msg, "msg")
        if name == "set":
            return self._dispatch_set(payload)
        elif name == "get":
            return self._dispatch_get(payload)
        elif name == "schema_get":
            return self._dispatch_schema_get(payload)
        elif name == "manifest_get":
            return self._dispatch_manifest_get(payload)
        return []

    def _dispatch_schema_get(self, msg):
        # An empty root addresses every server on the transport
        if msg.root and msg.root != self._name:
            return []
        return [pb.Message(schema=pb.Schema(hash=self.schema_hash, root=self._name))]

    def _dispatch_manifest_get(self, msg):
        if msg.root != self._name:
            return []
        data = self.manifest.encode("utf-8")
        chunks = [data[i:i + _MANIFEST_CHUNK] for i in range(0, len(data), _MANIFEST_CHUNK)]
        return [
            pb.Message(manifest_chunk=pb.ManifestChunk(data=chunk, last=(i == len(chunks) - 1)))
            for i, chunk in enumerate(chunks)
        ]

    def _dispatch_set(self, set_msg):
        result = find_param(self._root, self._name, set_msg.path)
        if result is None:
            return []

        obj, param_name, desc = result

        # Action
        if isinstance(desc, Action):
            desc(obj)
            return []

        # Group — can't set a group
        if desc is None:
            return []

        # Check if value is present
        value_field, _ = betterproto.which_one_of(set_msg.value, "kind")
        if not value_field:
            return []

        wire_val, wire_type = _extract_value(set_msg.value)

        # Coerce
        coerced = _coerce(wire_val, wire_type, desc.param_type)
        if coerced is None:
            return []

        # Set (descriptor handles clamping + auto-notify)
        setattr(obj, param_name, coerced)
        return []

    def _dispatch_get(self, get_msg):
        result = find_param(self._root, self._name, get_msg.path)
        if result is None:
            return []

        obj, param_name, desc = result

        if desc is None:
            # Group — walk subtree, send updates for all leaves
            return self._get_subtree(obj, get_msg.path)

        if isinstance(desc, Action):
            return []

        val = getattr(obj, param_name)
        return [_make_update(get_msg.path, val, desc.param_type)]

    def _get_subtree(self, group_obj, path_str):
        """Walk a group subtree and return Update messages for all leaf params."""
        prefix = path_str.rstrip("/")
        updates = []

        def visitor(path, name, desc, obj):
            if desc is None:
                return None  # group node, keep walking
            if isinstance(desc, Action):
                return None  # skip actions
            # path contains (name, obj) tuples from root to here (exclusive)
            # The first entry is the group root itself, which is already in prefix
            full_path = prefix
            for seg_name, _ in path[1:]:
                full_path += "/" + seg_name
            full_path += "/" + name

            val = getattr(obj, name)
            updates.append(_make_update(full_path, val, desc.param_type))
            return None

        walk_tree(group_obj, "", visitor)
        return updates

    def _notify(self, path, value, param_type):
        """Called by descriptors on __set__. Sends Update to clients."""
        self._send_message(_make_update(path, value, param_type))

    def _send_message(self, msg):
        if self._send:
            self._send(_codec.encode(msg))

    def feed(self, data):
        """Feed raw bytes from a client. Internally decodes and dispatches."""
        self._decoder.feed(data)

    def bind(self, send_fn):
        """Bind a send function: send_fn(bytes) -> None"""
        self._send = send_fn
        self._decoder = _codec.Decoder(lambda msg: self._dispatch_and_send(msg))

    def _dispatch_and_send(self, msg):
        responses = self._dispatch_message(msg)
        for resp in responses:
            self._send_message(resp)
