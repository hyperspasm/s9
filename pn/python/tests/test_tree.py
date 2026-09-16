"""Tree descriptor and introspection tests."""
import pn
from pn._tree import find_param, walk_tree, _Param, Action, Group


# --- Test tree matching C test_pn.c ---

class Sub:
    level = pn.Int(default=50, min=0, max=100, step=1)
    pan = pn.Float(default=0.0, min=-1.0, max=1.0, step=0.01)


class Root:
    gain = pn.Float(default=0.0, min=-72.0, max=6.0, step=0.1)
    mute = pn.Bool(default=False)
    input = pn.Enum(labels=["mic", "line"], default=0)
    name = pn.String(default="", max_len=32)
    sub = pn.Group(Sub)

    @pn.Action
    def reset(self):
        pass


# --- Descriptor tests ---

def test_float_default():
    r = Root()
    assert r.gain == 0.0


def test_float_set_get():
    r = Root()
    r.gain = 3.0
    assert r.gain == 3.0


def test_float_clamp_low():
    r = Root()
    r.gain = -100.0
    assert r.gain == -72.0


def test_float_clamp_high():
    r = Root()
    r.gain = 100.0
    assert r.gain == 6.0


def test_int_set_get():
    r = Root()
    r.sub.level = 75
    assert r.sub.level == 75


def test_int_clamp_low():
    r = Root()
    r.sub.level = -10
    assert r.sub.level == 0


def test_int_clamp_high():
    r = Root()
    r.sub.level = 200
    assert r.sub.level == 100


def test_bool_set_get():
    r = Root()
    r.mute = True
    assert r.mute is True


def test_enum_set_get():
    r = Root()
    r.input = 1
    assert r.input == 1


def test_enum_clamp():
    r = Root()
    r.input = 99
    assert r.input == 1  # clamped to count-1


def test_string_set_get():
    r = Root()
    r.name = "hello"
    assert r.name == "hello"


def test_group_access():
    r = Root()
    assert r.sub.level == 50
    assert r.sub.pan == 0.0


# --- Find tests ---

def test_find_root():
    r = Root()
    result = find_param(r, "root", "root")
    assert result is not None
    obj, name, desc = result
    assert obj is r
    assert name == "root"


def test_find_leaf():
    r = Root()
    result = find_param(r, "root", "root/gain")
    assert result is not None
    obj, name, desc = result
    assert name == "gain"
    assert isinstance(desc, pn.Float)


def test_find_nested():
    r = Root()
    result = find_param(r, "root", "root/sub/level")
    assert result is not None
    obj, name, desc = result
    assert name == "level"
    assert isinstance(desc, pn.Int)


def test_find_wrong_root():
    r = Root()
    assert find_param(r, "root", "other/gain") is None


def test_find_nonexistent():
    r = Root()
    assert find_param(r, "root", "root/nope") is None


def test_find_slashes():
    r = Root()
    assert find_param(r, "root", "/root/gain") is not None
    assert find_param(r, "root", "root/gain/") is not None
    assert find_param(r, "root", "root//gain") is not None
    assert find_param(r, "root", "//root///gain//") is not None


def test_find_empty():
    r = Root()
    assert find_param(r, "root", "") is None
    assert find_param(r, "root", None) is None


# --- Walk tests ---

def test_walk_visits_all():
    r = Root()
    visited = []

    def visitor(path, name, desc, obj):
        visited.append(name)
        return None

    walk_tree(r, "root", visitor)
    # Should visit: root group, then its params in order
    assert "root" in visited
    assert "gain" in visited
    assert "mute" in visited
    assert "input" in visited
    assert "name" in visited
    assert "reset" in visited
    assert "sub" in visited
    assert "level" in visited
    assert "pan" in visited


def test_walk_early_stop():
    r = Root()
    visited = []

    def visitor(path, name, desc, obj):
        visited.append(name)
        if name == "mute":
            return True
        return None

    result = walk_tree(r, "root", visitor)
    assert result is True
    assert "root" in visited
    assert "gain" in visited
    assert "mute" in visited
    assert "input" not in visited
