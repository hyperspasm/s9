"""Deterministic float32 test signals, as the C tests generate them.

Samples are computed in double and rounded once to float32. Additive shapes
sum their harmonics in double before rounding, and a table of partials
(recipes.py makes them) is summed the same way. Noise comes from xorshift32
so both sides produce the same samples from the same seed.
"""
import math

import numpy as np

SINE, SAWTOOTH, SQUARE, TWIN, NOISE, SILENCE, PARTIALS, IMPULSE = range(8)
KIND_NAMES = ["sine", "sawtooth", "square", "twin", "noise", "silence", "partials", "impulse"]


def sine(n, fs, freq, amp=1.0, phase=0.0):
    t = np.arange(n, dtype=np.float64) / fs
    return (amp * np.sin(2.0 * math.pi * freq * t + phase)).astype(np.float32)


def sawtooth(n, fs, freq, amp=1.0, harmonics=10, phase=0.0):
    """Band-limited: harmonics k=1..H with amplitude amp*2/(pi*k), alternating sign."""
    t = np.arange(n, dtype=np.float64) / fs
    x = np.zeros(n, dtype=np.float64)
    for k in range(1, harmonics + 1):
        x += (amp * 2.0 / (math.pi * k)) * (-1.0) ** (k + 1) * np.sin(2.0 * math.pi * k * freq * t + k * phase)
    return x.astype(np.float32)


def square(n, fs, freq, amp=1.0, harmonics=10, phase=0.0):
    """Band-limited: odd harmonics k=1,3,5.. up to the H-th harmonic, amplitude amp*4/(pi*k)."""
    t = np.arange(n, dtype=np.float64) / fs
    x = np.zeros(n, dtype=np.float64)
    for k in range(1, harmonics + 1, 2):
        x += (amp * 4.0 / (math.pi * k)) * np.sin(2.0 * math.pi * k * freq * t + k * phase)
    return x.astype(np.float32)


def twin(n, fs, freq, freq2, amp=1.0, phase=0.0):
    t = np.arange(n, dtype=np.float64) / fs
    x = amp * np.sin(2.0 * math.pi * freq * t + phase) + amp * np.sin(2.0 * math.pi * freq2 * t + phase)
    return x.astype(np.float32)


def noise(n, seed, amp=1.0):
    """Uniform in [-amp, amp) from xorshift32 seeded with seed (nonzero)."""
    x = np.empty(n, dtype=np.float32)
    s = seed & 0xFFFFFFFF
    for i in range(n):
        s ^= (s << 13) & 0xFFFFFFFF
        s ^= s >> 17
        s ^= (s << 5) & 0xFFFFFFFF
        x[i] = np.float32(amp * ((s >> 8) * (2.0 / 16777216.0) - 1.0))
    return x


def silence(n):
    return np.zeros(n, dtype=np.float32)


def partials(n, fs, table, amp=1.0):
    """The sum of a table of (frequency, amplitude, phase) partials, times amp: with a
    table of unit RMS, as recipes.py makes them, amp is the signal's RMS."""
    t = np.arange(n, dtype=np.float64) / fs
    x = np.zeros(n, dtype=np.float64)
    for f, a, p in table:
        x += (amp * a) * np.sin(2.0 * math.pi * f * t + p)
    return x.astype(np.float32)


def impulse(n, index, amp=1.0):
    """One sample of amp at index in silence."""
    x = np.zeros(n, dtype=np.float32)
    if 0 <= index < n:
        x[index] = np.float32(amp)
    return x


def generate(kind, n, fs, freq=0.0, freq2=0.0, amp=1.0, phase=0.0, harmonics=10, seed=1, noise_amp=0.0, table=None,
             index=0):
    """A signal of the kind, with noise of amplitude noise_amp from seed added
    when that is positive: each part rounded to float32, then their sum."""
    if kind == SINE:
        x = sine(n, fs, freq, amp, phase)
    elif kind == SAWTOOTH:
        x = sawtooth(n, fs, freq, amp, harmonics, phase)
    elif kind == SQUARE:
        x = square(n, fs, freq, amp, harmonics, phase)
    elif kind == TWIN:
        x = twin(n, fs, freq, freq2, amp, phase)
    elif kind == NOISE:
        x = noise(n, seed, amp)
    elif kind == SILENCE:
        x = silence(n)
    elif kind == PARTIALS:
        x = partials(n, fs, table, amp)
    elif kind == IMPULSE:
        x = impulse(n, index, amp)
    else:
        raise ValueError(kind)
    if noise_amp > 0.0:
        x = x + noise(n, seed, noise_amp)
    return x


def rms(x):
    """RMS of a frame, accumulated in double."""
    x = x.astype(np.float64)
    return math.sqrt(float(np.dot(x, x)) / len(x)) if len(x) else 0.0
