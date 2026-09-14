#!/usr/bin/env python3
"""Measure the constant-Q readout as the spectrogram uses it, with promises that fail the
run and figures that are only reported.

    python validation/scripts/measure_constant_q.py [--quiet]

Promises: a full-scale tone reads within LEVEL_DB of 0 dB at its nearest column
wherever its ridge spans at least RIDGE_COLUMNS columns, which is everywhere but the
top of the range, where the windows are as short as they get and the ridges narrower
than the columns can sample; what the kernels' truncation leaves out, the difference
between the kernel readout and the definition, stays below FLOOR_DB of full scale for
every tone and every column; a click read by no column's window reads below FLOOR_DB
everywhere. Reported: where the constant-Q regime begins and ends, the kernels' size
and build time, how low a tone can read where its ridge is narrow, which columns see
a click at each distance from the centre, and how deep the dip between two tones is
at one, one and a half, and two semitones apart.
"""
import math
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
import numpy as np  # noqa: E402

from s9 import constant_q, signals, window  # noqa: E402

FS, N, MIN_WINDOW, RESOLUTION, THRESHOLD_DB = 48000, 32768, 800, 1.0, -100.0
F_LO, F_HI, COLUMNS = 20.0, 20000.0, 1200
LEVEL_DB = 0.2       # a tone reads within this of its amplitude at its nearest column
RIDGE_COLUMNS = 5    # where its ridge at half amplitude spans at least this many columns
FLOOR_DB = -95.0     # the truncation's residue stays below this: the threshold, and the leakage of a tone just past a kernel's cut
TONES = 40           # tones measured, spread in log frequency
CLICK_OFFSETS_MS = (10.0, 50.0, 100.0, 300.0)
DIP_FREQUENCIES = (100.0, 300.0, 1000.0, 3000.0)
DIP_SEMITONES = (1.0, 1.5, 2.0)


def report(quiet, line):
    if not quiet:
        print(line)


def main():
    quiet = "--quiet" in sys.argv[1:]
    cfg = constant_q.Config(fs=FS, n=N, min_window=MIN_WINDOW, resolution=RESOLUTION, function=window.BLACKMAN_HARRIS,
                            threshold_db=THRESHOLD_DB)
    freqs = constant_q.frequencies(F_LO, F_HI, COLUMNS)
    windows = [constant_q.window_length(cfg, f) for f in freqs]
    failures = []

    t0 = time.time()
    kernels = constant_q.kernels(cfg, freqs)
    built = time.time() - t0
    taps = [len(k.taps) for k in kernels]
    report(quiet, f"{COLUMNS} columns from {F_LO:g} to {F_HI:g} Hz, resolution {RESOLUTION:g} semitone, "
                  f"window {window.NAMES[cfg.function]} ({window.bandwidth(cfg.function):.3f} bins at half amplitude)")
    longest = max(i for i in range(COLUMNS) if windows[i] == N)
    shortest = min(i for i in range(COLUMNS) if windows[i] == MIN_WINDOW)
    report(quiet, f"the frame's whole {N} samples up to {freqs[longest]:.1f} Hz, constant Q to {freqs[shortest]:.1f} Hz, "
                  f"then {MIN_WINDOW} samples")
    report(quiet, f"kernels: {sum(taps)} taps in all, {min(taps)} to {max(taps)} per column, built in {built:.1f} s")

    # tones: level at the nearest column where the ridge is wide enough to sample, and the residue everywhere
    rng = np.random.default_rng(1)
    columns_per_semitone = COLUMNS / (12.0 * math.log2(F_HI / F_LO))
    semitone = 2.0 ** (1.0 / 12.0) - 1.0
    wide_level, narrow_level, narrow_from, worst_residue = 0.0, 0.0, None, 0.0
    for i in range(TONES):
        f = 25.0 * (18000.0 / 25.0) ** (i / (TONES - 1))
        x = signals.sine(N, FS, f, 1.0, float(rng.uniform(0.0, 2.0 * math.pi)))
        ref = constant_q.transform(x, cfg, freqs)
        got = constant_q.readout(np.fft.rfft(x.astype(np.float64)), kernels)
        level = abs(constant_q.db(got).max())
        nearest = int(np.argmin(np.abs(freqs - f)))
        ridge = window.bandwidth(cfg.function) * FS / windows[nearest] / (f * semitone) * columns_per_semitone
        if ridge >= RIDGE_COLUMNS:
            wide_level = max(wide_level, level)
        else:
            narrow_level = max(narrow_level, level)
            narrow_from = f if narrow_from is None else narrow_from
        worst_residue = max(worst_residue, float(np.abs(got - ref).max()))
    residue_db = 20.0 * math.log10(max(worst_residue, constant_q.FLOOR))
    report(quiet, f"tones: nearest column within {wide_level:.3f} dB of full scale where the ridge spans {RIDGE_COLUMNS} columns"
                  + (f", within {narrow_level:.2f} dB from {narrow_from:.0f} Hz up where it is narrower" if narrow_from else ""))
    report(quiet, f"truncation residue at most {residue_db:.1f} dB")
    if wide_level > LEVEL_DB:
        failures.append(f"a tone read {wide_level:.3f} dB off at its nearest column, more than {LEVEL_DB}")
    if residue_db > FLOOR_DB:
        failures.append(f"the truncation residue reached {residue_db:.1f} dB, above {FLOOR_DB}")

    # clicks: seen only by windows that hold them
    for ms in CLICK_OFFSETS_MS:
        offset = int(round(ms * FS / 1000.0))
        x = signals.impulse(N, N // 2 + offset, 1.0)
        ref = constant_q.transform(x, cfg, freqs)
        got = constant_q.readout(np.fft.rfft(x.astype(np.float64)), kernels)
        seen = [i for i in range(COLUMNS) if abs(ref[i]) > 0.0]
        unseen = [i for i in range(COLUMNS) if abs(ref[i]) == 0.0]
        leak = 20.0 * math.log10(max(max(abs(got[i]) for i in unseen), constant_q.FLOOR)) if unseen else -math.inf
        if seen:
            report(quiet, f"a click {ms:g} ms after the centre is seen up to {freqs[seen[-1]]:.1f} Hz, "
                          f"window {windows[seen[-1]]} samples; elsewhere it reads at most {leak:.1f} dB")
        else:
            report(quiet, f"a click {ms:g} ms after the centre is seen by no column; it reads at most {leak:.1f} dB")
        if unseen and leak > FLOOR_DB:
            failures.append(f"a click {ms:g} ms off reached {leak:.1f} dB in a column whose window excludes it")

    # two tones: the dip between them
    report(quiet, "two equal tones, the dip between their ridges below the lower of them, in phase / in quadrature:")
    for f in DIP_FREQUENCIES:
        line = f"  at {f:6.0f} Hz:"
        for semis in DIP_SEMITONES:
            f2 = f * 2.0 ** (semis / 12.0)
            dips = []
            for phase2 in (0.0, 0.5 * math.pi):
                x = signals.sine(N, FS, f, 0.5) + signals.sine(N, FS, f2, 0.5, phase2)
                levels = constant_q.db(constant_q.readout(np.fft.rfft(x.astype(np.float64)), kernels))
                lo = int(np.argmin(np.abs(freqs - f)))
                hi = int(np.argmin(np.abs(freqs - f2)))
                peak_lo, peak_hi = levels[max(lo - 3, 0):lo + 4].max(), levels[max(hi - 3, 0):hi + 4].max()
                between = levels[lo:hi + 1].min()
                dips.append(min(peak_lo, peak_hi) - between)
            line += f"   {semis:g} st {dips[0]:5.1f} / {dips[1]:5.1f} dB"
        report(quiet, line)

    if failures:
        for f in failures:
            print("FAIL: " + f)
        return 1
    print("ok" if quiet else "every promise holds")
    return 0


if __name__ == "__main__":
    sys.exit(main())
