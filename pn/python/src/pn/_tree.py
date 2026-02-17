from ._types import ParamType


class _Param:
    """Base descriptor for parameter tree leaf nodes."""

    param_type = ParamType.NONE

    def __init__(self, *, default=None):
        self._default = default
        self._name = None
        self._on_set_cb = None

    def __set_name__(self, owner, name):
        self._name = name

    def __get__(self, obj, objtype=None):
        if obj is None:
            return self
        return obj.__dict__.get(self._name, self._default)

    def __set__(self, obj, value):
        value = self._coerce(value)
        value = self._clamp(value)
        obj.__dict__[self._name] = value
        if self._on_set_cb is not None:
            self._on_set_cb(obj, value)
        server = getattr(obj, '_pn_server', None)
        if server is not None:
            path = obj._pn_path + "/" + self._name
            server._notify(path, value, self.param_type)

    def _coerce(self, value):
        return value

    def _clamp(self, value):
        return value

    def on_set(self, fn):
        """Decorator to register a callback when this param is set."""
        self._on_set_cb = fn
        return fn


class Float(_Param):
    param_type = ParamType.FLOAT

    def __init__(self, *, default=0.0, min=0.0, max=1.0, step=0.0):
        super().__init__(default=default)
        self.min = min
        self.max = max
        self.step = step

    def _coerce(self, value):
        return float(value)

    def _clamp(self, value):
        if value < self.min:
            return self.min
        if value > self.max:
            return self.max
        return value


class Int(_Param):
    param_type = ParamType.INT

    def __init__(self, *, default=0, min=0, max=100, step=1):
        super().__init__(default=default)
        self.min = min
        self.max = max
        self.step = step

    def _coerce(self, value):
        return int(value)

    def _clamp(self, value):
        if value < self.min:
            return self.min
        if value > self.max:
            return self.max
        return value


class Bool(_Param):
    param_type = ParamType.BOOL

    def __init__(self, *, default=False):
        super().__init__(default=default)

    def _coerce(self, value):
        return bool(value)


class Enum(_Param):
    param_type = ParamType.ENUM

    def __init__(self, *, labels, default=0):
        super().__init__(default=default)
        self.labels = list(labels)

    def _coerce(self, value):
        return int(value)

    def _clamp(self, value):
        if not self.labels:
            raise ValueError("enum has no labels")
        if value >= len(self.labels):
            return len(self.labels) - 1
        if value < 0:
            return 0
        return value


class String(_Param):
    param_type = ParamType.STRING

    def __init__(self, *, default="", max_len=256):
        super().__init__(default=default)
        self.max_len = max_len

    def _coerce(self, value):
        return str(value)

    def _clamp(self, value):
        if len(value) > self.max_len:
            return value[:self.max_len]
        return value


class Action:
    """Decorator that marks a method as a pn action."""
    param_type = ParamType.ACTION

    def __init__(self, fn):
        self._fn = fn
        self._name = fn.__name__

    def __set_name__(self, owner, name):
        self._name = name

    def __get__(self, obj, objtype=None):
        if obj is None:
            return self
        return self._fn.__get__(obj, objtype)

    def __call__(self, obj):
        return self._fn(obj)


class Group:
    """Declares a child group in the parameter tree."""
    param_type = ParamType.NONE

    def __init__(self, cls):
        self._cls = cls
        self._name = None

    def __set_name__(self, owner, name):
        self._name = name

    def __get__(self, obj, objtype=None):
        if obj is None:
            return self
        if self._name not in obj.__dict__:
            child = self._cls()
            obj.__dict__[self._name] = child
            server = getattr(obj, '_pn_server', None)
            if server is not None:
                _bind_tree(child, obj._pn_path + "/" + self._name, server)
        return obj.__dict__[self._name]

    def __set__(self, obj, value):
        obj.__dict__[self._name] = value


def _bind_tree(obj, path, server):
    """Install server reference and path on all tree objects for auto-notify."""
    obj._pn_path = path
    obj._pn_server = server
    for name, attr in _iter_params(obj):
        if isinstance(attr, Group):
            child = getattr(obj, name)
            _bind_tree(child, path + "/" + name, server)
        elif not isinstance(attr, (_Param, Action)):
            # Dynamic child
            _bind_tree(attr, path + "/" + name, server)


def _iter_params(obj):
    """Yield (name, descriptor_or_child) for all pn-relevant attributes of obj.

    Uses class __dict__ to preserve definition order (Python 3.7+).
    """
    cls = type(obj)
    seen = set()
    for klass in cls.__mro__:
        for name, attr in klass.__dict__.items():
            if name in seen:
                continue
            if isinstance(attr, (_Param, Action, Group)):
                seen.add(name)
                yield name, attr
    # Also check instance dict for dynamically added children
    for name, val in obj.__dict__.items():
        if name.startswith("_"):
            continue
        if name in seen:
            continue
        if hasattr(val, "__dict__") and any(
            isinstance(v, (_Param, Action, Group))
            for v in vars(type(val)).values()
        ):
            yield name, val


def walk_tree(obj, name, visitor, path=None):
    """Walk the parameter tree depth-first, calling visitor(path, name, descriptor, obj) for each node.

    visitor receives:
        path: list of (name, obj) tuples from root to current node (exclusive)
        name: name of current node
        desc: the descriptor (_Param, Action, Group) or None for dynamic children
        obj: the object instance owning this node

    Returns the first non-None return value from visitor, or None.
    """
    if path is None:
        path = []

    # Visit the current node itself (as a group)
    result = visitor(path, name, None, obj)
    if result is not None:
        return result

    path = path + [(name, obj)]

    for child_name, attr in _iter_params(obj):
        if isinstance(attr, Group):
            child_obj = getattr(obj, child_name)
            result = walk_tree(child_obj, child_name, visitor, path)
            if result is not None:
                return result
        elif isinstance(attr, (_Param, Action)):
            result = visitor(path, child_name, attr, obj)
            if result is not None:
                return result
        else:
            # Dynamic child (instance dict object that looks like a pn tree node)
            result = walk_tree(attr, child_name, visitor, path)
            if result is not None:
                return result

    return None


def find_param(root_obj, root_name, path_str):
    """Find a parameter by path string. Returns (obj, name, descriptor) or None.

    For groups, returns (group_obj, group_name, None).
    For leaves, returns (owner_obj, param_name, descriptor).
    """
    if not path_str:
        return None

    # Normalize path: strip slashes, split on /, filter empties
    parts = [p for p in path_str.split("/") if p]
    if not parts:
        return None

    # First segment must match root name
    if parts[0] != root_name:
        return None

    if len(parts) == 1:
        return (root_obj, root_name, None)

    # Walk remaining segments
    current = root_obj
    for i, segment in enumerate(parts[1:], 1):
        # Look for matching child
        found = False
        for child_name, attr in _iter_params(current):
            if child_name == segment:
                if isinstance(attr, Group):
                    current = getattr(current, child_name)
                    found = True
                    break
                elif isinstance(attr, (_Param, Action)):
                    if i == len(parts) - 1:
                        return (current, child_name, attr)
                    else:
                        return None  # trying to descend into a leaf
                else:
                    # Dynamic child
                    current = attr
                    found = True
                    break
        if not found:
            return None

    # Ended on a group
    return (current, parts[-1], None)
