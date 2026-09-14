"""s9's window functions, reproduced from src/window_function.c.

Every step mirrors the C: shapes evaluated in double and stored as float32,
the skew warp interpolating float32 values in double, and the normalization
accumulating the sum in float32 in index order. Identifiers follow the C enum.
"""
import functools
import math

import numpy as np

BARTLETT, HANN, WELCH, HAMMING, BLACKMAN_HARRIS, BLACKMAN, RECTANGULAR = range(7)
NAMES = ["bartlett", "hann", "welch", "hamming", "blackman_harris", "blackman", "rectangular"]

_2PI = 2.0 * math.pi


def _shape(function, length):
    out = np.empty(length, dtype=np.float32)
    if length == 1 and function != RECTANGULAR:
        out[0] = 1.0
        return out
    if function == RECTANGULAR:
        out[:] = 1.0
        return out
    m = length - 1
    for i in range(length):
        if function == BARTLETT:
            half = 0.5 * m
            v = 1.0 - abs((i - half) / half)
        elif function == HANN:
            v = 0.5 - 0.5 * math.cos(_2PI / m * i)
        elif function == WELCH:
            half = 0.5 * m
            v = 1.0 - ((i - half) / half) ** 2
        elif function == HAMMING:
            v = 0.54 - 0.46 * math.cos(_2PI / m * i)
        elif function == BLACKMAN_HARRIS:
            v = (0.35875 - 0.48829 * math.cos(_2PI / m * i)
                 + 0.14128 * math.cos(2 * _2PI / m * i)
                 - 0.01168 * math.cos(3 * _2PI / m * i))
        elif function == BLACKMAN:
            v = (0.42323 - 0.49755 * math.cos(_2PI / m * i)
                 + 0.07922 * math.cos(2 * _2PI / m * i))
        else:
            raise ValueError(function)
        out[i] = np.float32(v)
    return out


def _skew(window, skew):
    if skew == 0.0 or len(window) < 2:
        return window
    length = len(window)
    gamma = math.exp(-float(skew))
    tmp = window.copy()
    out = np.empty(length, dtype=np.float32)
    inv_len = 1.0 / (length - 1)
    for i in range(length):
        t = i * inv_len
        t_warped = 1.0 - (1.0 - t) ** gamma
        src = t_warped * (length - 1)
        idx = int(src)
        frac = src - idx
        if idx + 1 < length:
            out[i] = np.float32((1.0 - frac) * float(tmp[idx]) + frac * float(tmp[idx + 1]))
        else:
            out[i] = tmp[length - 1]
    return out


def _normalize(window):
    total = np.float32(0.0)
    for w in window:
        total = np.float32(total + w)
    factor = np.float32(np.float32(len(window)) / total)
    return (window * factor).astype(np.float32)


def create(function, length, normalize=True, skew=0.0):
    """The window as float32, exactly as create_window_function() fills it."""
    window = _shape(function, length)
    if skew != 0.0:
        window = _skew(window, skew)
    if normalize:
        window = _normalize(window)
    return window


BANDWIDTH_LENGTH = 4096      # the length the bandwidth is measured at
BANDWIDTH_STEP = 1.0 / 64    # bins between points of the scan


@functools.lru_cache(maxsize=None)
def bandwidth(function):
    """The width of the window's transform at half its peak, in bins of the
    window's own length: the width a tone's ridge has through it. Found by
    scanning the transform outward from the peak in steps of BANDWIDTH_STEP
    at BANDWIDTH_LENGTH and interpolating the crossing, as the C does."""
    w = create(function, BANDWIDTH_LENGTH, normalize=False).astype(np.float64)
    i = np.arange(BANDWIDTH_LENGTH, dtype=np.float64)
    peak = float(np.sum(w))
    nu, prev = 0.0, peak
    while True:
        nu += BANDWIDTH_STEP
        mag = abs(np.sum(w * np.exp(-2j * math.pi * nu * i / BANDWIDTH_LENGTH)))
        if mag < 0.5 * peak:
            return 2.0 * (nu - BANDWIDTH_STEP * (mag - 0.5 * peak) / (mag - prev))
        prev = mag
