import betterproto

from ._types import ParamType
from . import _proto as pb
from . import _codec


class Client:
    def __init__(self, on_update=None):
        """Create a client.

        on_update: optional callback fn(path: str, value, type: ParamType) -> None
        """
        self._send = None
        self._on_update = on_update
        self._decoder = None

    def bind(self, send_fn):
        """Bind a send function: send_fn(bytes) -> None"""
        self._send = send_fn
        self._decoder = _codec.Decoder(self._on_message)

    def on_update(self, fn):
        """Register or replace the update callback. Can be used as a decorator."""
        self._on_update = fn
        return fn

    def feed(self, data):
        """Feed raw bytes from the server."""
        self._decoder.feed(data)

    def set(self, path, value):
        """Send a Set message. Value type is inferred from Python type."""
        v = pb.Value()
        if isinstance(value, bool):
            v.bool_val = value
        elif isinstance(value, int):
            v.int_val = value
        elif isinstance(value, float):
            v.float_val = value
        elif isinstance(value, str):
            v.string_val = value
        else:
            raise TypeError(f"unsupported value type: {type(value)}")
        msg = pb.Message(set=pb.Set(path=path, value=v))
        self._send_message(msg)

    def set_float(self, path, value):
        msg = pb.Message(set=pb.Set(path=path, value=pb.Value(float_val=float(value))))
        self._send_message(msg)

    def set_int(self, path, value):
        msg = pb.Message(set=pb.Set(path=path, value=pb.Value(int_val=int(value))))
        self._send_message(msg)

    def set_bool(self, path, value):
        msg = pb.Message(set=pb.Set(path=path, value=pb.Value(bool_val=bool(value))))
        self._send_message(msg)

    def set_enum(self, path, value):
        msg = pb.Message(set=pb.Set(path=path, value=pb.Value(enum_val=int(value))))
        self._send_message(msg)

    def set_string(self, path, value):
        msg = pb.Message(set=pb.Set(path=path, value=pb.Value(string_val=str(value))))
        self._send_message(msg)

    def get(self, path):
        """Send a Get request."""
        msg = pb.Message(get=pb.Get(path=path))
        self._send_message(msg)

    def action(self, path):
        """Trigger an action (Set with no value)."""
        msg = pb.Message(set=pb.Set(path=path))
        self._send_message(msg)

    def _send_message(self, msg):
        if self._send:
            self._send(_codec.encode(msg))

    def _on_message(self, msg):
        if self._on_update is None:
            return
        name, payload = betterproto.which_one_of(msg, "msg")
        if name != "update":
            return
        value_field, value = betterproto.which_one_of(payload.value, "kind")
        if not value_field:
            return
        type_map = {
            "bool_val": ParamType.BOOL,
            "int_val": ParamType.INT,
            "float_val": ParamType.FLOAT,
            "enum_val": ParamType.ENUM,
            "string_val": ParamType.STRING,
        }
        ptype = type_map.get(value_field)
        if ptype is not None:
            self._on_update(payload.path, value, ptype)
