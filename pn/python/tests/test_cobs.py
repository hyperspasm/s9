"""COBS tests — ported from test_pn.c"""
import pytest
from pn._cobs import encode, decode, Decoder


def roundtrip(data: bytes):
    """Encode then decode, verify original data is recovered."""
    encoded = encode(data)
    # Strip delimiter for decode
    assert encoded[-1] == 0x00
    decoded = decode(encoded[:-1])
    assert decoded == data


def test_roundtrip_basic():
    roundtrip(bytes([0x01, 0x02, 0x03, 0x04, 0x05]))


def test_roundtrip_empty():
    roundtrip(b"")


def test_roundtrip_single_zero():
    roundtrip(bytes([0x00]))


def test_roundtrip_all_zeros():
    roundtrip(bytes(32))


def test_roundtrip_no_zeros():
    roundtrip(bytes(range(1, 65)))


def test_roundtrip_254_bytes():
    # 254 non-zero bytes — exactly one COBS block
    roundtrip(bytes((i % 254) + 1 for i in range(254)))


def test_roundtrip_255_bytes():
    # 255 non-zero bytes — crosses a COBS block boundary
    roundtrip(bytes((i % 254) + 1 for i in range(255)))


def test_roundtrip_zeros_at_boundaries():
    roundtrip(bytes([0x00, 0x01, 0x02, 0x00, 0x03, 0x00]))


def test_multiple_frames():
    data1 = bytes([0xAA, 0xBB])
    data2 = bytes([0xCC, 0xDD, 0xEE])

    received = []
    decoder = Decoder(lambda pkt: received.append(pkt))

    enc1 = encode(data1)
    enc2 = encode(data2)

    # Feed everything at once
    decoder.feed(enc1 + enc2)
    assert len(received) == 2
    assert received[0] == data1
    assert received[1] == data2


def test_consecutive_delimiters():
    data = bytes([0x01, 0x02])

    received = []
    decoder = Decoder(lambda pkt: received.append(pkt))

    enc = encode(data)
    # Prepend extra delimiters
    decoder.feed(bytes([0x00, 0x00, 0x00]) + enc)
    assert len(received) == 1
    assert received[0] == data


def test_corrupted_crc():
    data = bytes([0x01, 0x02, 0x03])
    enc = bytearray(encode(data))

    # Corrupt a byte in the encoded frame (before the delimiter)
    enc[1] ^= 0xFF

    received = []
    decoder = Decoder(lambda pkt: received.append(pkt))
    decoder.feed(bytes(enc))
    assert len(received) == 0


def test_byte_at_a_time():
    data = bytes([0xDE, 0xAD, 0x00, 0xBE, 0xEF])

    received = []
    decoder = Decoder(lambda pkt: received.append(pkt))

    enc = encode(data)
    # Feed one byte at a time
    for b in enc:
        decoder.feed(bytes([b]))

    assert len(received) == 1
    assert received[0] == data


def test_decode_invalid_zero():
    with pytest.raises(ValueError):
        decode(bytes([0x00, 0x01]))


def test_decode_truncated():
    with pytest.raises(ValueError):
        decode(bytes([0x05, 0x01]))  # code says 4 data bytes but only 1 present


def test_decoder_overflow():
    """Frames exceeding max_frame_size are silently dropped."""
    received = []
    decoder = Decoder(lambda pkt: received.append(pkt), max_frame_size=16)

    # A frame that exceeds the limit
    big_data = bytes(range(1, 32))
    decoder.feed(encode(big_data))
    assert len(received) == 0

    # A normal frame still works after overflow
    small_data = bytes([0x01, 0x02])
    decoder.feed(encode(small_data))
    assert len(received) == 1
    assert received[0] == small_data
