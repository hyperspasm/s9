#!/usr/bin/env python3
"""How often noise passes for a harmonic: frames of noise are read at the
harmonics of a fundamental, for transform sizes, windows, and prominence
thresholds, and the rate of false readouts per harmonic per frame is
printed. Also the reach of each window's main lobe.

    python validation/scripts/measure_prominence.py
"""
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
from s9 import harmonics, signals, window  # noqa: E402

FS, F0, COUNT, FRAMES = 44100, 220.0, 10, 100
SIZES = (4096, 32768)
WINDOWS = ((window.HANN, "hann"), (window.BLACKMAN_HARRIS, "blackman_harris"))
SHOULDERS = (8, 16, 32, 48)
THRESHOLDS = (12.0, 15.0, 18.0)
SEARCH_MIN_BINS, SEARCH_RATIO = 3, 0.01   # as the component searches


def main():
    print(f"{'window':<16} {'N':>6} {'reach':>5} {'shoulder':>8}  " + "  ".join(f"{t:>6.0f} dB" for t in THRESHOLDS))
    for fn, name in WINDOWS:
        for n in SIZES:
            w = window.create(fn, n, normalize=True, skew=0.0)
            table = harmonics.correction_table(w)
            reach = harmonics.lobe_reach(w)
            delta_f = FS / n
            spectra = []
            for seed in range(1, FRAMES + 1):
                x = signals.noise(n, seed, 0.1)
                spectra.append(harmonics.spectrum((x * w).astype(np.float32)))
            for shoulder in SHOULDERS:
                hits = [0] * len(THRESHOLDS)
                tested = 0
                for bins in spectra:
                    for i, t in enumerate(THRESHOLDS):
                        for k in range(1, COUNT + 1):
                            search = max(SEARCH_MIN_BINS, math.ceil(k * F0 * SEARCH_RATIO / delta_f))
                            # one harmonic at a time so each gets its own search width
                            reads = harmonics.readouts(bins, n, FS, k * F0, 1, search, table, reach, shoulder, t)
                            hits[i] += 1 if reads[0][0] else 0
                    tested += COUNT
                rates = "  ".join(f"{h / tested:9.5f}" for h in hits)
                print(f"{name:<16} {n:>6} {reach:>5} {shoulder:>8}  {rates}")


if __name__ == "__main__":
    main()
