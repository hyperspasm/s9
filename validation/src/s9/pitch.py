"""Pitch estimation from a windowed frame, and the conditioning of the
per-frame estimates into a clean fundamental.

The estimate works on the normalized autocorrelation of the frame: the
inverse transform of the windowed frame's power spectrum, divided lag by lag
by the window's own autocorrelation so the taper does not tilt the peaks
toward short lags, then divided by its value at lag 0. A periodic signal
scores 1 at its period and at every multiple. Both autocorrelations are
circular, so the frame's tail correlates with its head; a window that tapers
to zero makes that contribution vanish, one that does not (Hamming) leaves a
small bias at the longest lags. Peaks are the maxima of the positive regions; the first
peak within a tolerance of the highest is the period, refined by a parabola
through it and its neighbours. Preferring the first keeps a periodic
signal's own period over its multiples; the tolerance lets a twin tone's
common period beat the shoulders of its beat.
"""
import math

import numpy as np


def window_acf(w):
    """Circular autocorrelation of the window at every lag, in double."""
    wd = w.astype(np.float64)
    return np.fft.irfft(np.abs(np.fft.rfft(wd)) ** 2, n=len(wd))


def nacf_from_frame(xw, w):
    """Normalized autocorrelation of a windowed float32 frame for every lag
    0..N-1, in double: the frame's autocorrelation over the window's, scaled
    to 1 at lag 0. Zero wherever the window's autocorrelation vanishes."""
    x = xw.astype(np.float64)
    n = len(x)
    r = np.fft.irfft(np.abs(np.fft.rfft(x)) ** 2, n=n)   # r[t] = sum_j x[j] x[(j+t) mod n]
    rw = window_acf(w)
    out = np.zeros(n)
    nz = rw > 0.0
    out[nz] = r[nz] / rw[nz]
    if out[0] > 0.0:
        out /= out[0]
    else:
        out[:] = 0.0
    return out


def estimate(nacf, lag_min, lag_max, tolerance=0.02):
    """(period, clarity) of the first peak in [lag_min, lag_max] within
    tolerance of the highest, or (0.0, 0.0) when the function has no positive
    peak there. A peak cut off by lag_max counts. The initial lobe around lag
    0 is skipped."""
    nsdf = nacf
    n = len(nsdf)
    lag_max = min(lag_max, n - 1)
    i = 1
    while i <= lag_max and nsdf[i] > 0.0:
        i += 1
    candidates = []
    in_peak = False
    best = None
    while i <= lag_max:
        v = nsdf[i]
        if v > 0.0:
            if not in_peak:
                in_peak = True
                best = None
            if i >= lag_min and (best is None or v > best[1]):
                best = (i, v)
        elif in_peak:
            if best is not None:
                candidates.append(best)
            in_peak = False
        i += 1
    if in_peak and best is not None:
        candidates.append(best)
    if not candidates:
        return 0.0, 0.0
    threshold = max(v for _, v in candidates) - tolerance
    for p, v in candidates:
        if v >= threshold:
            break
    return refine(nsdf, p)


def refine(nsdf, p):
    """Parabolic interpolation around index p: (fractional lag, peak value)."""
    if p < 1 or p + 1 >= len(nsdf):
        return float(p), float(min(max(nsdf[p], 0.0), 1.0))
    y0, y1, y2 = float(nsdf[p - 1]), float(nsdf[p]), float(nsdf[p + 1])
    denom = y0 - 2.0 * y1 + y2
    delta = 0.5 * (y0 - y2) / denom if denom != 0.0 else 0.0
    value = y1 - 0.25 * (y0 - y2) * delta
    return p + delta, min(max(value, 0.0), 1.0)


def cents(freq):
    return 1200.0 * math.log2(freq)


class ConditionerConfig:
    def __init__(self, gate_rms, clarity_on, clarity_off, hold_frames, octave_tolerance_cents,
                 smoothing, deadband_cents):
        self.gate_rms = gate_rms                    # frames quieter than this are unpitched
        self.clarity_on = clarity_on                # clarity needed to become valid
        self.clarity_off = clarity_off              # clarity needed to stay valid
        self.hold_frames = hold_frames              # unpitched frames tolerated before validity ends
        self.octave_tolerance_cents = octave_tolerance_cents  # an estimate this close to double or half is folded
        self.smoothing = smoothing                  # per-frame one-pole coefficient toward the estimate, in cents
        self.deadband_cents = deadband_cents        # the reported value moves only when the smoothed one differs by more


class Conditioner:
    """Turns per-frame (period, clarity, rms) into (valid, frequency).

    While valid, the frequency follows the estimates through the smoother and
    the deadband; while invalid, it holds its last value, so a display can
    keep a stable picture.
    """

    def __init__(self, config):
        self.cfg = config
        self.valid = False
        self.hold = 0
        self.smoothed = None
        self.reported = None

    def update(self, period, clarity, rms, sample_rate):
        cfg = self.cfg
        freq = sample_rate / period if period > 0.0 else 0.0
        candidate = None
        if rms >= cfg.gate_rms and freq > 0.0:
            if (not self.valid and clarity >= cfg.clarity_on) or (self.valid and clarity >= cfg.clarity_off):
                candidate = freq

        if candidate is None:
            if self.valid:
                self.hold += 1
                if self.hold > cfg.hold_frames:
                    self.valid = False
        else:
            c = cents(candidate)
            if self.valid:
                d = c - self.smoothed
                if abs(d - 1200.0) <= cfg.octave_tolerance_cents:
                    c -= 1200.0
                elif abs(d + 1200.0) <= cfg.octave_tolerance_cents:
                    c += 1200.0
                self.smoothed += cfg.smoothing * (c - self.smoothed)
            else:
                self.smoothed = c
                self.reported = c
                self.valid = True
            self.hold = 0
            if abs(self.smoothed - self.reported) > cfg.deadband_cents:
                self.reported = self.smoothed

        frequency = 2.0 ** (self.reported / 1200.0) if self.reported is not None else 0.0
        return self.valid, frequency
