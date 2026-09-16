from . import _cobs
from . import _proto as pb


def encode(msg: pb.Message) -> bytes:
    """Serialize a Message to wire format: proto → COBS+CRC → framed bytes."""
    return _cobs.encode(bytes(msg))


def decode(payload: bytes) -> pb.Message:
    """Deserialize wire payload (after COBS+CRC decode) to a Message."""
    return pb.Message().parse(payload)


class Decoder:
    """Streaming message decoder. Feed raw bytes, get Message objects via callback."""

    def __init__(self, on_message):
        self._on_message = on_message
        self._cobs = _cobs.Decoder(self._on_packet)

    def _on_packet(self, payload: bytes) -> None:
        msg = decode(payload)
        self._on_message(msg)

    def feed(self, data: bytes) -> None:
        self._cobs.feed(data)
