"""The tracker s9 runs today, through its Python oracle: a Blackman-Harris window of
0.75 s, one transform per 10 ms hop, the harmonic-series estimator over its magnitudes,
and the conditioner at the defaults in src/fundamental.h. The C tracker matches the
oracle to its goldens, so this is the baseline of what exists. It has no lock stage:
the conditioned fundamental is reported as both. Each frame's value is stamped at the
frame's end, when the tracker could first show it.
"""
from __future__ import annotations

import math

import numpy as np

from s9 import conditioner as cond
from s9 import harmonic_series as hs
from s9 import harmonics
from s9 import window as win

from ..score import Trajectory

WINDOW_S = 0.75
HOP_S = 0.01
CONDITIONER = dict(gate_rms=0.01, harmonicity_on=0.5, harmonicity_off=0.4, hold_s=0.05,
                   octave_tolerance_cents=30.0, smoothing_s=0.5, deadband_cents=0.05, snap_cents=50.0)


def estimate(audio, fs):
    n = 1 << int(math.log2(WINDOW_S * fs))
    hop = int(HOP_S * fs)
    function = getattr(win, "BLACKMAN_HARRIS", win.NAMES.index("blackman_harris"))
    w = win.create(function, n)
    table = harmonics.correction_table(w)
    reach = harmonics.lobe_reach(w)
    cfg = hs.Config()
    c = cond.Conditioner(cond.Config(**CONDITIONER))
    x = audio.astype(np.float64)
    times, f0s, valid = [], [], []
    for start in range(0, x.shape[0] - n + 1, hop):
        frame = x[start:start + n]
        db = harmonics.magnitude_db(harmonics.spectrum(frame * w), n)
        est = hs.estimate(db, fs / n, reach, table, cfg)
        rms = float(np.sqrt(np.mean(frame * frame)))
        ok, f = c.update(est.f0, est.harmonicity, rms, HOP_S)
        times.append((start + n) / fs)
        f0s.append(float(f) if ok and f else 0.0)
        valid.append(bool(ok))
    t, f0 = np.array(times), np.array(f0s)
    return Trajectory(t, f0, np.ones_like(f0), f0, np.array(valid))
