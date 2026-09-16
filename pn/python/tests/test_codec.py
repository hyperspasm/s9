"""Codec tests — encode/decode Message roundtrip."""
import betterproto
from pn._codec import encode, decode, Decoder
from pn._proto import Message, Set, Get, Update, Value


def test_roundtrip_set_float():
    msg = Message(set=Set(path="mixer/ch1/gain", value=Value(float_val=3.0)))
    encoded = encode(msg)
    received = []
    dec = Decoder(lambda m: received.append(m))
    dec.feed(encoded)
    assert len(received) == 1
    name, payload = betterproto.which_one_of(received[0], "msg")
    assert name == "set"
    assert payload.path == "mixer/ch1/gain"
    _, val = betterproto.which_one_of(payload.value, "kind")
    assert val == 3.0


def test_roundtrip_get():
    msg = Message(get=Get(path="mixer/ch1"))
    received = []
    dec = Decoder(lambda m: received.append(m))
    dec.feed(encode(msg))
    assert len(received) == 1
    name, payload = betterproto.which_one_of(received[0], "msg")
    assert name == "get"
    assert payload.path == "mixer/ch1"


def test_roundtrip_update():
    msg = Message(update=Update(path="mixer/ch1/gain", value=Value(float_val=-6.0)))
    received = []
    dec = Decoder(lambda m: received.append(m))
    dec.feed(encode(msg))
    assert len(received) == 1
    name, payload = betterproto.which_one_of(received[0], "msg")
    assert name == "update"
    assert payload.path == "mixer/ch1/gain"
    _, val = betterproto.which_one_of(payload.value, "kind")
    assert val == -6.0


def test_roundtrip_set_string():
    msg = Message(set=Set(path="root/name", value=Value(string_val="hello")))
    received = []
    dec = Decoder(lambda m: received.append(m))
    dec.feed(encode(msg))
    assert len(received) == 1
    name, payload = betterproto.which_one_of(received[0], "msg")
    assert name == "set"
    _, val = betterproto.which_one_of(payload.value, "kind")
    assert val == "hello"


def test_multiple_messages():
    msgs = [
        Message(set=Set(path="a", value=Value(int_val=1))),
        Message(get=Get(path="b")),
        Message(update=Update(path="c", value=Value(bool_val=True))),
    ]
    received = []
    dec = Decoder(lambda m: received.append(m))
    for msg in msgs:
        dec.feed(encode(msg))
    assert len(received) == 3
