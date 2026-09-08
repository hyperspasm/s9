"""Per-harmonic readouts from a spectrum.

Magnitudes are in dB relative to a full-scale sine: with s9's normalized
window, a sine of amplitude a reads 20 log10(a). For harmonic k of the
reference f0, the loudest bin within `search` bins of k*f0 is refined by a
parabola through the dB values of it and its neighbours. A parabola is not
the shape of a window's main lobe, so the refinement is biased by an amount
that depends only on the window and on the offset the parabola reports; the
correction table gives that bias and is subtracted.
"""
import math

import numpy as np

CORRECTION_POINTS = 33   # table entries over the offset range 0..0.5


def spectrum(xw):
    """rfft of a windowed float32 frame, in double."""
    return np.fft.rfft(xw.astype(np.float64))


def magnitude_db(bins, n):
    scale = 2.0 / n
    mag = np.abs(bins) * scale
    with np.errstate(divide="ignore"):
        return 20.0 * np.log10(mag)


def parabola(y0, y1, y2):
    """(offset from the centre sample, peak value) of the parabola through three samples."""
    denom = y0 - 2.0 * y1 + y2
    d = 0.5 * (y0 - y2) / denom if denom != 0.0 else 0.0
    return d, y1 - 0.25 * (y0 - y2) * d


def lobe_db(w, delta):
    """The window's transform magnitude, in dB relative to its peak, delta bins from its centre."""
    n = len(w)
    j = np.arange(n, dtype=np.float64)
    wd = w.astype(np.float64)
    value = abs(np.sum(wd * np.exp(-2j * math.pi * delta * j / n)))
    return 20.0 * math.log10(value / np.sum(wd))


def correction_table(w, points=CORRECTION_POINTS):
    """Bias of the parabolic refinement on this window, indexed by the offset
    the parabola reports, uniformly over 0..0.5: (db_bias, offset_bias).
    The parabola's dB peak is high by db_bias and its offset is off by
    offset_bias, both away from the true peak."""
    reported, db_bias, offset_bias = [], [], []
    for true in np.linspace(0.0, 0.5, 4 * points):
        y = [lobe_db(w, k - true) for k in (-1, 0, 1)]
        d, peak = parabola(*y)
        reported.append(d)
        db_bias.append(peak)            # the true peak is 0 dB by construction
        offset_bias.append(d - true)
    grid = np.linspace(0.0, 0.5, points)
    return np.interp(grid, reported, db_bias), np.interp(grid, reported, offset_bias)


def corrected(d, peak_db, table):
    """Apply the correction table to a parabola's (offset, peak dB)."""
    db_bias, offset_bias = table
    grid = np.linspace(0.0, 0.5, len(db_bias))
    a = min(abs(d), 0.5)
    db = peak_db - float(np.interp(a, grid, db_bias))
    off = d - math.copysign(float(np.interp(a, grid, offset_bias)), d)
    return off, db


def readouts(bins, n, fs, f0, harmonics, search, table):
    """A list of (found, frequency, db) for harmonics 1..harmonics. A harmonic
    is found when the loudest bin within `search` bins of it is a local
    maximum; on a slope, the skirt of something louder nearby, there is no
    peak to read."""
    db = magnitude_db(bins, n)
    last = n // 2 - 1          # the highest bin a parabola can be centred on
    delta_f = fs / n
    out = []
    for k in range(1, harmonics + 1):
        centre = int(round(k * f0 / delta_f))
        if centre > last or centre < 1:
            out.append((False, 0.0, 0.0))
            continue
        lo = max(1, centre - search)
        hi = min(last, centre + search)
        p = lo + int(np.argmax(db[lo:hi + 1]))
        y0, y1, y2 = float(db[p - 1]), float(db[p]), float(db[p + 1])
        if not (y1 > y0 and y1 >= y2):
            out.append((False, 0.0, 0.0))
            continue
        if not all(math.isfinite(v) for v in (y0, y1, y2)):
            out.append((True, p * delta_f, y1))
            continue
        d, peak = parabola(y0, y1, y2)
        off, value = corrected(d, peak, table)
        out.append((True, (p + off) * delta_f, value))
    return out
