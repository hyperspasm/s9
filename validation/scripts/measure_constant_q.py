#!/usr/bin/env python3
"""Measure the constant-Q readout as the spectrogram uses it, with promises that fail the
run and figures that are only reported.

    python validation/scripts/measure_constant_q.py [--quiet]

At each of the hops measured, with the shortest window two hops: a full-scale tone
anywhere in the range reads within LEVEL_DB of 0 dB at the column it falls in, which the
readouts across a column guarantee where the ridges are narrower than the columns; what
the kernels' truncation leaves out, the difference between the kernel readout and the
definition, stays below FLOOR_DB of full scale for every tone and every column; a click
read by no column's window reads below FLOOR_DB everywhere. Reported: where the
constant-Q regime begins and ends, the kernels' size and build time, the readouts per
column, which columns see a click at each distance from the centre, and how deep the
dip between two tones is at one, one and a half, and two semitones apart.
"""
import math
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
import numpy as np  # noqa: E402

from s9 import constant_q, signals, window  # noqa: E402

FS, N, RESOLUTION, THRESHOLD_DB, READOUTS_PER_RIDGE = 48000, 32768, 1.0, -100.0, 3.0
HOPS_MS = (10, 60)   # the hops measured: the default, and the slowest
MIN_HOPS = 2         # the shortest window, in hops
F_LO, F_HI, COLUMNS = 20.0, 20000.0, 1200
LEVEL_DB = 0.8       # a tone reads within this of its amplitude at its column
FLOOR_DB = -95.0     # the truncation's residue stays below this: the threshold, and the leakage of a tone just past a kernel's cut
TONES = 40           # tones measured, spread in log frequency
CLICK_OFFSETS_MS = (10.0, 50.0, 100.0, 300.0)
DIP_FREQUENCIES = (100.0, 300.0, 1000.0, 3000.0)
DIP_SEMITONES = (1.0, 1.5, 2.0)


def report(quiet, line):
    if not quiet:
        print(line)


def measure(quiet, hop_ms, failures):
    min_window = MIN_HOPS * hop_ms * FS // 1000
    cfg = constant_q.Config(fs=FS, n=N, min_window=min_window, resolution=RESOLUTION, function=window.BLACKMAN_HARRIS,
                            threshold_db=THRESHOLD_DB, readouts_per_ridge=READOUTS_PER_RIDGE)
    edges = constant_q.edges(F_LO, F_HI, COLUMNS)
    bands = [constant_q.band(cfg, edges[c], edges[c + 1]) for c in range(COLUMNS)]
    windows = [length for length, _ in bands]
    counts = [len(freqs) for _, freqs in bands]

    t0 = time.time()
    columns = constant_q.kernels(cfg, edges)
    built = time.time() - t0
    report(quiet, f"hop {hop_ms} ms, shortest window {min_window} samples: "
                  f"the frame's whole {N} samples up to {edges[max(c for c in range(COLUMNS) if windows[c] == N) + 1]:.1f} Hz, "
                  f"constant Q to {edges[min(c for c in range(COLUMNS) if windows[c] == min_window)]:.1f} Hz")
    more = [c for c in range(COLUMNS) if counts[c] > 1]
    report(quiet, f"  {constant_q.readouts(columns)} readouts, {constant_q.taps(columns)} taps in all through transforms of "
                  f"{', '.join(str(lv) for lv in columns.levels)}, built in {built:.1f} s"
                  + (f"; columns from {edges[more[0]]:.0f} Hz up read at 2 to {max(counts)} frequencies" if more else ""))

    # tones: the level at the column each falls in, and the residue everywhere
    rng = np.random.default_rng(1)
    worst_level, worst_residue = 0.0, 0.0
    for i in range(TONES):
        f = 25.0 * (18000.0 / 25.0) ** ((i + 0.37) / TONES)
        x = signals.sine(N, FS, f, 1.0, float(rng.uniform(0.0, 2.0 * math.pi)))
        ref = constant_q.transform(x, cfg, edges)
        got = constant_q.readout(x, columns)
        c = int(np.searchsorted(edges, f) - 1)
        worst_level = max(worst_level, abs(float(constant_q.db(got[c:c + 1])[0])))
        worst_residue = max(worst_residue, float(np.abs(got - ref).max()))
    residue_db = 20.0 * math.log10(max(worst_residue, constant_q.FLOOR))
    report(quiet, f"  tones: read within {worst_level:.3f} dB of full scale at their column; truncation residue at most {residue_db:.1f} dB")
    if worst_level > LEVEL_DB:
        failures.append(f"at a {hop_ms} ms hop a tone read {worst_level:.3f} dB off at its column, more than {LEVEL_DB}")
    if residue_db > FLOOR_DB:
        failures.append(f"at a {hop_ms} ms hop the truncation residue reached {residue_db:.1f} dB, above {FLOOR_DB}")

    # clicks: seen only by windows that hold them
    for ms in CLICK_OFFSETS_MS:
        offset = int(round(ms * FS / 1000.0))
        x = signals.impulse(N, N // 2 + offset, 1.0)
        ref = constant_q.transform(x, cfg, edges)
        got = constant_q.readout(x, columns)
        seen = [c for c in range(COLUMNS) if ref[c] > 0.0]
        unseen = [c for c in range(COLUMNS) if ref[c] == 0.0]
        leak = 20.0 * math.log10(max(max(got[c] for c in unseen), constant_q.FLOOR)) if unseen else -math.inf
        if seen:
            report(quiet, f"  a click {ms:g} ms after the centre is seen up to {edges[seen[-1] + 1]:.1f} Hz, "
                          f"window {windows[seen[-1]]} samples; elsewhere it reads at most {leak:.1f} dB")
        else:
            report(quiet, f"  a click {ms:g} ms after the centre is seen by no column; it reads at most {leak:.1f} dB")
        if unseen and leak > FLOOR_DB:
            failures.append(f"at a {hop_ms} ms hop a click {ms:g} ms off reached {leak:.1f} dB in a column whose window excludes it")

    # two tones: the dip between them
    report(quiet, "  two equal tones, the dip between their ridges below the lower of them, in phase / in quadrature:")
    for f in DIP_FREQUENCIES:
        line = f"    at {f:6.0f} Hz:"
        for semis in DIP_SEMITONES:
            f2 = f * 2.0 ** (semis / 12.0)
            dips = []
            for phase2 in (0.0, 0.5 * math.pi):
                x = signals.sine(N, FS, f, 0.5) + signals.sine(N, FS, f2, 0.5, phase2)
                levels = constant_q.db(constant_q.readout(x, columns))
                lo = int(np.searchsorted(edges, f) - 1)
                hi = int(np.searchsorted(edges, f2) - 1)
                peak_lo, peak_hi = levels[max(lo - 3, 0):lo + 4].max(), levels[max(hi - 3, 0):hi + 4].max()
                between = levels[lo:hi + 1].min()
                dips.append(min(peak_lo, peak_hi) - between)
            line += f"   {semis:g} st {dips[0]:5.1f} / {dips[1]:5.1f} dB"
        report(quiet, line)


def main():
    quiet = "--quiet" in sys.argv[1:]
    report(quiet, f"{COLUMNS} columns from {F_LO:g} to {F_HI:g} Hz, resolution {RESOLUTION:g} semitone, "
                  f"window blackman_harris ({window.bandwidth(window.BLACKMAN_HARRIS):.3f} bins at half amplitude), "
                  f"{READOUTS_PER_RIDGE:g} readouts per ridge")
    failures = []
    for hop_ms in HOPS_MS:
        measure(quiet, hop_ms, failures)
    if failures:
        for f in failures:
            print("FAIL: " + f)
        return 1
    print("ok" if quiet else "every promise holds")
    return 0


if __name__ == "__main__":
    sys.exit(main())
