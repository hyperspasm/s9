from enum import IntEnum


class ParamType(IntEnum):
    NONE = 0    # group
    ACTION = 1
    BOOL = 2
    INT = 3
    FLOAT = 4
    ENUM = 5
    STRING = 6
