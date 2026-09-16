"""A constant-Q readout from one long transform: the level in each of a set of
frequency bands, the columns, through a window sized for the band's frequency,
every window centred on the middle of the same frame.

The definition. A column spanning f_lo to f_hi has a window of length L, the
length at which a tone's ridge through the window is `resolution` semitones
wide at half amplitude at the band's centre, sqrt(f_lo f_hi):
L = bandwidth(window) * fs / (f * (2^(resolution/12) - 1)), no shorter than
min_window and no longer than the frame, rounded to an even number and placed
at the middle of the frame. The column is read at frequencies spread evenly in
log frequency across the band, as many as it takes for them to be no farther
apart than the ridge's width over `readouts_per_ridge`, so a tone anywhere in
the band reads within a fraction of a dB of its level; one readout at the
centre when the band is narrow against the ridge. Each readout is the windowed
transform at exactly its frequency, scaled so that a full-scale sine reads 1:

    y = (2 / sum w) * sum_i x[start + i] * w[i] * exp(-2 pi j f (start + i) / fs)

and the column's level is 20 log10 of the largest |y|. Low columns therefore
have long windows and trade time for frequency resolution; high ones have
windows as short as min_window and trade the other way; the lowest are limited
by the frame.

The computation (Brown and Puckette, 1992). With k[n] a readout's window
shifted to its frequency, y = sum_n x[n] conj(k[n]) = (1/N) sum_k X[k] conj(K[k])
exactly, where X and K are the N-point transforms of a frame and of k, for any
N that holds the window. K is concentrated about f, so only the taps between
the first and the last that stand above threshold_db of its peak, over the real
half of the spectrum, are kept, and a readout is a short dot product with the
frame's transform. What the truncation leaves out is the residue
measure_constant_q.py reports. Since K's width in bins is N over the window's
length, each readout is taken through the shortest transform that holds its
window: the frame itself, or the middle of it at a power of two, all centred on
the same instant. The kernels then stay a few dozen taps however long the
frame is, and the transforms of every level together cost about twice the
longest.
"""
import functools
import math

import numpy as np

from . import window

FLOOR = 1e-20   # a level below this is silence, -400 dB


class Config:
    def __init__(self, fs=48000.0, n=32768, min_window=960, resolution=1.0, function=window.BLACKMAN_HARRIS,
                 threshold_db=-100.0, readouts_per_ridge=3.0):
        self.fs = fs
        self.n = n                                  # the frame and its transform's length, the longest window
        self.min_window = min_window                # the shortest window, samples
        self.resolution = resolution                # the width of a tone's ridge at half amplitude, semitones
        self.function = function                    # the window function of every column
        self.threshold_db = threshold_db            # a kernel's taps below this of its peak are dropped
        self.readouts_per_ridge = readouts_per_ridge  # readouts across a band are at most the ridge's width over this apart


def edges(f_lo, f_hi, count):
    """The edges of count columns spaced evenly in log frequency from f_lo to f_hi: count + 1 values."""
    return np.array([f_lo * (f_hi / f_lo) ** (i / count) for i in range(count + 1)], dtype=np.float64)


def window_length(cfg, f):
    """The window length for a ridge at f, even, within [min_window, n]."""
    semitones = 2.0 ** (cfg.resolution / 12.0) - 1.0
    length = window.bandwidth(cfg.function) * cfg.fs / (f * semitones)
    length = min(max(length, float(cfg.min_window)), float(cfg.n))
    length = 2 * int(math.floor(length / 2.0 + 0.5))
    return min(max(length, 2), cfg.n)


def band(cfg, lo, hi):
    """A column's window length and the frequencies it is read at."""
    length = window_length(cfg, math.sqrt(lo * hi))
    ridge = window.bandwidth(cfg.function) * cfg.fs / length   # Hz, at half amplitude
    count = max(1, int(math.ceil(cfg.readouts_per_ridge * (hi - lo) / ridge)))
    return length, [lo * (hi / lo) ** ((i + 0.5) / count) for i in range(count)]


@functools.lru_cache(maxsize=None)
def _window(function, length):
    return window.create(function, length, normalize=False).astype(np.float64)


def _placed(cfg, length):
    """A window of the length as float64 with its start in the frame and its scale."""
    w = _window(cfg.function, length)
    return w, cfg.n // 2 - length // 2, 2.0 / float(np.sum(w))


def readout_value(x, cfg, length, f):
    """The definition of one readout: the frame x through a window of the length at f, a complex value."""
    w, start, scale = _placed(cfg, length)
    i = np.arange(start, start + length, dtype=np.float64)
    seg = x[start:start + length].astype(np.float64)
    return scale * np.sum(seg * w * np.exp(-2j * math.pi * f * i / cfg.fs))


def column(x, cfg, lo, hi):
    """The definition of a column: the largest magnitude among its readouts."""
    length, freqs = band(cfg, lo, hi)
    return max(abs(readout_value(x, cfg, length, f)) for f in freqs)


def transform(x, cfg, edges):
    """The definition for every column, magnitudes."""
    return np.array([column(x, cfg, edges[c], edges[c + 1]) for c in range(len(edges) - 1)])


def db(y):
    """Levels in dB relative to a full-scale sine, FLOOR for silence."""
    return 20.0 * np.log10(np.maximum(np.abs(y), FLOOR))


def level(cfg, length):
    """The transform a window of the length is read through: the shortest
    power of two that holds it, or the frame itself when none within it does."""
    p = 2
    while p < length:
        p *= 2
    return p if p <= cfg.n else cfg.n


class Kernel:
    def __init__(self, level, first, taps):
        self.level = level    # the transform's length
        self.first = first    # the bin of the first tap
        self.taps = taps      # conj(K) / level over the kept run, complex


class Column:
    def __init__(self, length, kernels):
        self.length = length      # the window length
        self.kernels = kernels    # one per readout, all on the same level


class Kernels:
    def __init__(self, cfg, columns):
        self.cfg = cfg
        self.columns = columns
        self.levels = sorted({k.level for col in columns for k in col.kernels})


def kernel(cfg, length, f):
    """A readout's kernel: the transform, at its level, of its window shifted
    to f and placed in the middle, kept between the first and last bins above
    the threshold, ready to dot with the level's real-input transform."""
    n = level(cfg, length)
    w, _, scale = _placed(cfg, length)
    start = n // 2 - length // 2
    i = np.arange(start, start + length, dtype=np.float64)
    k = np.zeros(n, dtype=np.complex128)
    k[start:start + length] = scale * w * np.exp(2j * math.pi * f * i / cfg.fs)
    K = np.fft.fft(k)[:n // 2 + 1]
    mag = np.abs(K)
    above = np.nonzero(mag >= mag.max() * 10.0 ** (cfg.threshold_db / 20.0))[0]
    first, last = int(above[0]), int(above[-1])
    return Kernel(n, first, np.conj(K[first:last + 1]) / n)


def kernels(cfg, edges):
    """The columns' kernels."""
    columns = []
    for c in range(len(edges) - 1):
        length, freqs = band(cfg, edges[c], edges[c + 1])
        columns.append(Column(length, [kernel(cfg, length, f) for f in freqs]))
    return Kernels(cfg, columns)


def taps(kernels):
    """Taps in all, the cost of a readout."""
    return sum(len(k.taps) for col in kernels.columns for k in col.kernels)


def readouts(kernels):
    """Readouts in all."""
    return sum(len(col.kernels) for col in kernels.columns)


def readout(frame, kernels):
    """Every column from the frame, n samples: magnitudes."""
    n = kernels.cfg.n
    frame = np.asarray(frame, dtype=np.float64)
    spectra = {}
    for lv in kernels.levels:
        offset = n // 2 - lv // 2
        spectra[lv] = np.fft.rfft(frame[offset:offset + lv])
    return np.array([max(abs(np.dot(spectra[k.level][k.first:k.first + len(k.taps)], k.taps)) for k in col.kernels)
                     for col in kernels.columns])
