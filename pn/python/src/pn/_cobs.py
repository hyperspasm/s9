from ._crc import crc16


def encode(data: bytes) -> bytes:
    """COBS encode data with CRC-16 appended. Returns framed bytes with 0x00 delimiter."""
    crc = crc16(data)
    # Append CRC as little-endian
    payload = data + bytes([crc & 0xFF, (crc >> 8) & 0xFF])

    out = bytearray()
    code_idx = len(out)
    out.append(0)  # placeholder for first code byte
    code = 1

    for b in payload:
        if b == 0:
            out[code_idx] = code
            code_idx = len(out)
            out.append(0)  # placeholder for next code byte
            code = 1
        else:
            out.append(b)
            code += 1
            if code == 0xFF:
                out[code_idx] = code
                code_idx = len(out)
                out.append(0)  # placeholder for next code byte
                code = 1

    out[code_idx] = code
    out.append(0x00)  # frame delimiter
    return bytes(out)


def decode(frame: bytes) -> bytes:
    """COBS decode a frame (without delimiter). Returns payload after CRC verification.

    Raises ValueError on invalid framing or CRC mismatch.
    """
    src = memoryview(frame)
    out = bytearray()
    pos = 0

    while pos < len(src):
        code = src[pos]
        pos += 1
        if code == 0:
            raise ValueError("unexpected zero in COBS frame")

        count = code - 1
        if pos + count > len(src):
            raise ValueError("COBS frame truncated")

        out.extend(src[pos:pos + count])
        pos += count

        if code < 0xFF and pos < len(src):
            out.append(0)

    if len(out) < 2:
        raise ValueError("COBS frame too short for CRC")

    payload = bytes(out[:-2])
    rx_crc = out[-2] | (out[-1] << 8)
    calc_crc = crc16(payload)
    if rx_crc != calc_crc:
        raise ValueError("CRC mismatch")

    return payload


class Decoder:
    """Streaming COBS decoder. Feed bytes, get packets via callback.

    max_frame_size limits the receive buffer (default 1024). Frames exceeding
    this are silently dropped, matching the C implementation's fixed-buffer
    overflow behaviour.
    """

    def __init__(self, on_packet, max_frame_size=1024):
        self._on_packet = on_packet
        self._buf = bytearray()
        self._max_frame_size = max_frame_size
        self._overflow = False

    def feed(self, data: bytes) -> None:
        for b in data:
            if b == 0x00:
                if self._buf and not self._overflow:
                    try:
                        payload = decode(bytes(self._buf))
                        self._on_packet(payload)
                    except ValueError:
                        pass
                self._buf.clear()
                self._overflow = False
            else:
                if len(self._buf) < self._max_frame_size:
                    self._buf.append(b)
                else:
                    self._overflow = True
