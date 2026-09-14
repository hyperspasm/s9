#!/usr/bin/env python3
"""How readouts behave over time on a sine with one weak harmonic in noise,
as the component reads them: the 32768-point window sliding a display frame
at a time over twenty seconds, the per-bin power averaged with a time
constant, the harmonics read from the average. For each averaging time,
threshold, and level of the weak harmonic above the floor: how often the
harmonics the signal does not have are read (false), how often the weak one
is (hit), and how often its state flips per second, bare and behind a gate
with hysteresis and hold.

    python validation/scripts/measure_averaging.py
"""
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
from s9 import harmonics, signals, window  # noqa: E402

FS, N, HOP = 44100, 32768, 735
F0, AMP, COUNT = 220.0, 0.5, 10
WEAK = 3                                  # the harmonic given a level
SECONDS = 20
NOISE_AMP = 0.01
TAUS = (0.0, 0.5, 1.0, 2.0)               # averaging time constants, seconds
THRESHOLDS = (4.0, 6.0, 8.0, 10.0, 12.0, 15.0)
LEVELS = (6.0, 9.0, 12.0, 15.0, 20.0)     # the weak harmonic above the floor's median, dB
GATES = ((10.0, 7.0), (10.0, 6.0), (12.0, 8.0), (12.0, 6.0), (15.0, 9.0))   # (on, off) thresholds
HOLD = 0.5                                # seconds below off before hiding
SEARCH_MIN_BINS, SEARCH_RATIO, SHOULDER = 3, 0.01, 32
WINDOW = window.BLACKMAN_HARRIS


def spectra(x, w):
    for start in range(0, len(x) - N + 1, HOP):
        yield np.abs(harmonics.spectrum((x[start:start + N] * w).astype(np.float32))) ** 2


def prominences(x, w, table, reach, delta_f):
    """Per averaging time, per harmonic, the prominence frame by frame."""
    alphas = [1.0 if tau == 0.0 else 1.0 - math.exp(-HOP / FS / tau) for tau in TAUS]
    avgs = [None] * len(TAUS)
    out = [[[] for _ in range(COUNT)] for _ in TAUS]
    searches = [max(SEARCH_MIN_BINS, math.ceil(k * F0 * SEARCH_RATIO / delta_f)) for k in range(1, COUNT + 1)]
    for power in spectra(x, w):
        for i, alpha in enumerate(alphas):
            avgs[i] = power.copy() if avgs[i] is None else avgs[i] + alpha * (power - avgs[i])
            db = harmonics.power_db(avgs[i], N)
            for k in range(1, COUNT + 1):
                r = harmonics.readouts_db(db, N, FS, k * F0, 1, searches[k - 1], table, reach, SHOULDER, -math.inf)
                out[i][k - 1].append(r[0][3])
    return out


def gated(prom, on, off, hold_frames):
    """The gate's state frame by frame over a prominence series."""
    shown, below, states = False, 0, []
    for p in prom:
        if p >= on:
            shown, below = True, 0
        elif shown:
            if p >= off:
                below = 0
            else:
                below += 1
                if below > hold_frames:
                    shown = False
        states.append(shown)
    return states


def flips_per_second(states):
    return sum(1 for a, b in zip(states, states[1:]) if a != b) / (len(states) * HOP / FS)


def main():
    w = window.create(WINDOW, N, normalize=True, skew=0.0)
    table = harmonics.correction_table(w)
    reach = harmonics.lobe_reach(w)
    delta_f = FS / N
    n = SECONDS * FS + N
    hold_frames = int(HOLD * FS / HOP)

    # the floor's median at the weak harmonic, from the noise alone
    noise = signals.noise(n, 3, NOISE_AMP)
    db = harmonics.power_db(next(spectra(noise, w)), N)
    centre = int(round(WEAK * F0 / delta_f))
    floor = float(np.median(db[centre - 200:centre + 200]))
    print(f"noise floor median {floor:.1f} dB at H{WEAK}; window reach {reach} bins; {SECONDS} s, {n // HOP} frames")

    print("bare: fraction of frames read, and flips per second, by threshold")
    print(f"{'level':>5} {'tau':>4}  " + "  ".join(f"{'false@%.0f' % t:>10} {'hit@%.0f' % t:>8}" for t in THRESHOLDS))
    results = {}
    for level in LEVELS:
        x = signals.sine(n, FS, F0, AMP, 0.3) + signals.sine(n, FS, WEAK * F0, 10 ** ((floor + level) / 20.0), 1.1) + noise
        prom = prominences(x, w, table, reach, delta_f)
        results[level] = prom
        for i, tau in enumerate(TAUS):
            empty = [k for k in range(1, COUNT + 1) if k not in (1, WEAK)]
            cells = []
            for thr in THRESHOLDS:
                false = np.mean([np.mean(np.array(prom[i][k - 1]) >= thr) for k in empty])
                hit = np.mean(np.array(prom[i][WEAK - 1]) >= thr)
                cells.append(f"{false:>10.4f} {hit:>8.3f}")
            print(f"{level:>5.0f} {tau:>4.1f}  " + "  ".join(cells))

    print("\ngated: fraction of frames the label is shown and its flips per second, for empty harmonics and the weak one")
    print(f"{'level':>5} {'tau':>4}  " + "  ".join(f"{'%.0f/%.0f' % g:>9} {'':>5}" for g in GATES))
    for level in LEVELS:
        prom = results[level]
        for i, tau in enumerate(TAUS):
            empty = [k for k in range(1, COUNT + 1) if k not in (1, WEAK)]
            cells = []
            for on, off in GATES:
                false = np.mean([np.mean(gated(prom[i][k - 1], on, off, hold_frames)) for k in empty])
                gate = gated(prom[i][WEAK - 1], on, off, hold_frames)
                cells.append(f"{false:>5.3f} {np.mean(gate):>5.3f} {flips_per_second(gate):>3.1f}")
            print(f"{level:>5.0f} {tau:>4.1f}  " + "  ".join(cells))


if __name__ == "__main__":
    main()
