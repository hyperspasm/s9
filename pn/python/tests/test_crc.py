"""CRC-16-CCITT tests — ported from test_pn.c"""
from pn._crc import crc16


def test_crc_empty():
    assert crc16(b"") == 0xFFFF


def test_crc_known_vector():
    # Standard CRC-16/CCITT-FALSE check value for "123456789"
    assert crc16(b"123456789") == 0x29B1


def test_crc_single_byte():
    assert crc16(b"\x00") != crc16(b"\xFF")


def test_crc_detects_change():
    data1 = bytes([0x01, 0x02, 0x03, 0x04])
    data2 = bytes([0x01, 0x02, 0xFF, 0x04])
    assert crc16(data1) != crc16(data2)


def test_crc_detects_reorder():
    assert crc16(bytes([0x01, 0x02])) != crc16(bytes([0x02, 0x01]))
