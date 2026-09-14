"""A constant-Q readout from one long transform: the level at each of a set of
frequencies through a window sized for that frequency, every window centred
on the middle of the same frame.

The definition. A column at frequency f has a window of length L, the length at
which a tone's ridge through the window is `resolution` semitones wide at half
amplitude: L = bandwidth(window) * fs / (f * (2^(resolution/12) - 1)), no
shorter than min_window and no longer than the frame, rounded to an even
number and placed at the middle of the frame. Its value is the windowed
transform at exactly f, scaled so that a full-scale sine at f reads 1:

    y = (2 / sum w) * sum_i x[start + i] * w[i] * exp(-2 pi j f (start + i) / fs)

and its level is 20 log10 |y|. Low columns therefore have long windows and
trade time for frequency resolution; high ones have windows as short as one
hop and trade the other way; the lowest are limited by the frame.

The computation (Brown and Puckette, 1992). With k[n] the column's window
shifted to its frequency, y = sum_n x[n] conj(k[n]) = (1/N) sum_k X[k] conj(K[k])
exactly, where X and K are the N-point transforms of the frame and of k. K is
concentrated about f, so only the taps between the first and the last that
stand above threshold_db of its peak, over the real half of the spectrum, are
kept, and a column is a short dot product with the frame's transform. What the
truncation leaves out is the residue measure_constant_q.py reports.
"""
import functools
import math

import numpy as np

from . import window

FLOOR = 1e-20   # a level below this is silence, -400 dB


class Config:
    def __init__(self, fs=48000.0, n=32768, min_window=800, resolution=1.0, function=window.BLACKMAN_HARRIS,
                 threshold_db=-100.0):
        self.fs = fs
        self.n = n                        # the frame and its transform's length, the longest window
        self.min_window = min_window      # the shortest window, samples
        self.resolution = resolution      # the width of a tone's ridge at half amplitude, semitones
        self.function = function          # the window function of every column
        self.threshold_db = threshold_db  # a kernel's taps below this of its peak are dropped


def frequencies(f_lo, f_hi, count):
    """count frequencies spaced evenly in log frequency from f_lo to f_hi inclusive."""
    if count == 1:
        return np.array([f_lo], dtype=np.float64)
    return np.array([f_lo * (f_hi / f_lo) ** (i / (count - 1)) for i in range(count)], dtype=np.float64)


def window_length(cfg, f):
    """The window length of the column at f, even, within [min_window, n]."""
    semitones = 2.0 ** (cfg.resolution / 12.0) - 1.0
    length = window.bandwidth(cfg.function) * cfg.fs / (f * semitones)
    length = min(max(length, float(cfg.min_window)), float(cfg.n))
    length = 2 * int(math.floor(length / 2.0 + 0.5))
    return min(max(length, 2), cfg.n)


@functools.lru_cache(maxsize=None)
def _window(function, length):
    return window.create(function, length, normalize=False).astype(np.float64)


def _placed(cfg, f):
    """The column's window as float64 with its start in the frame and its scale."""
    length = window_length(cfg, f)
    w = _window(cfg.function, length)
    return w, cfg.n // 2 - length // 2, 2.0 / float(np.sum(w))


def column(x, cfg, f):
    """The definition: the column at f of the frame x, a complex value."""
    w, start, scale = _placed(cfg, f)
    i = np.arange(start, start + len(w), dtype=np.float64)
    seg = x[start:start + len(w)].astype(np.float64)
    return scale * np.sum(seg * w * np.exp(-2j * math.pi * f * i / cfg.fs))


def transform(x, cfg, freqs):
    """The definition for every column."""
    return np.array([column(x, cfg, f) for f in freqs])


def db(y):
    """Levels in dB relative to a full-scale sine, FLOOR for silence."""
    return 20.0 * np.log10(np.maximum(np.abs(y), FLOOR))


class Kernel:
    def __init__(self, first, taps):
        self.first = first    # the bin of the first tap
        self.taps = taps      # conj(K) / N over the kept run, complex


def kernel(cfg, f):
    """The column's kernel: the transform of its window shifted to f, kept
    between the first and last bins above the threshold, ready to dot with
    the frame's real-input transform."""
    w, start, scale = _placed(cfg, f)
    i = np.arange(start, start + len(w), dtype=np.float64)
    k = np.zeros(cfg.n, dtype=np.complex128)
    k[start:start + len(w)] = scale * w * np.exp(2j * math.pi * f * i / cfg.fs)
    K = np.fft.fft(k)[:cfg.n // 2 + 1]
    mag = np.abs(K)
    above = np.nonzero(mag >= mag.max() * 10.0 ** (cfg.threshold_db / 20.0))[0]
    first, last = int(above[0]), int(above[-1])
    return Kernel(first, np.conj(K[first:last + 1]) / cfg.n)


def kernels(cfg, freqs):
    return [kernel(cfg, f) for f in freqs]


def taps(kernels):
    """Taps in all, the cost of a readout."""
    return sum(len(k.taps) for k in kernels)


def readout(X, kernels):
    """Every column from X, the frame's transform as numpy.fft.rfft gives it."""
    return np.array([np.dot(X[k.first:k.first + len(k.taps)], k.taps) for k in kernels])
