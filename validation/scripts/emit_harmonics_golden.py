#!/usr/bin/env python3
"""Emit test/golden/harmonics_golden.h: per-harmonic readouts and the
distortion from the spectra of synthetic signals.

    python validation/scripts/emit_harmonics_golden.py
"""
import os
import sys

import math

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
from s9 import harmonics, signals, window  # noqa: E402
from _emit import c_array, c_double, c_string, header, repo_path  # noqa: E402

FS, N, SEARCH, SHOULDER, PROMINENCE = 44100, 4096, 3, 32, 15.0

# (label, signal, window, reference f0, harmonics to read)
CASES = [
    ("saw_220",   dict(kind=signals.SAWTOOTH, freq=220.0, amp=0.5, phase=0.2, harmonics=10), (window.HAMMING, 0.0), 220.0, 12),
    ("square_300", dict(kind=signals.SQUARE, freq=300.0, amp=0.5, phase=0.5, harmonics=9), (window.HAMMING, 0.0), 300.0, 8),
    ("sine_5000",  dict(kind=signals.SINE, freq=5000.0, amp=1.0, phase=0.0), (window.HAMMING, 0.0), 5000.0, 6),
    ("sine_offbin", dict(kind=signals.SINE, freq=440.7, amp=0.25, phase=0.9), (window.HAMMING, 0.0), 440.7, 3),
    ("saw_110_bh", dict(kind=signals.SAWTOOTH, freq=110.0, amp=0.5, phase=0.0, harmonics=16), (window.BLACKMAN_HARRIS, 0.0), 110.0, 16),
    ("sine_noise", dict(kind=signals.SINE, freq=1000.0, amp=0.5, phase=0.3, seed=7, noise_amp=0.02), (window.HAMMING, 0.0), 1000.0, 6),
    ("saw_noise",  dict(kind=signals.SAWTOOTH, freq=220.0, amp=0.5, phase=0.1, harmonics=10, seed=11, noise_amp=0.005), (window.BLACKMAN_HARRIS, 0.0), 220.0, 10),
    ("square_noise", dict(kind=signals.SQUARE, freq=300.0, amp=0.5, phase=0.0, harmonics=9, seed=5, noise_amp=0.01), (window.BLACKMAN_HARRIS, 0.0), 300.0, 10),
]


def analytic_phase(kind, k):
    """The relative phase the generators give harmonic k: each harmonic is a
    sine, so its phase is -90 degrees, with 180 more for a sawtooth's even
    harmonics, whose sign alternates; less k times the fundamental's -90."""
    if kind == signals.SAWTOOTH:
        theta = -90.0 + 90.0 * k + (180.0 if k % 2 == 0 else 0.0)
    elif kind == signals.SQUARE:
        theta = -90.0 + 90.0 * k
    else:
        return None
    theta %= 360.0
    return theta - 360.0 if theta > 180.0 else theta


def wrap(deg):
    deg %= 360.0
    return deg - 360.0 if deg > 180.0 else deg


def main():
    parts = []
    parts.append("#define HARMONICS_GOLDEN_FS %d" % FS)
    parts.append("#define HARMONICS_GOLDEN_N %d" % N)
    parts.append("#define HARMONICS_GOLDEN_SEARCH %d" % SEARCH)
    parts.append("#define HARMONICS_GOLDEN_SHOULDER %d" % SHOULDER)
    parts.append("#define HARMONICS_GOLDEN_PROMINENCE %s" % c_double(PROMINENCE))
    parts.append("#define HARMONICS_GOLDEN_CORRECTION_POINTS %d" % harmonics.CORRECTION_POINTS)
    for fn, name in ((window.HAMMING, "hamming"), (window.BLACKMAN_HARRIS, "blackman_harris")):
        w = window.create(fn, N, normalize=True, skew=0.0)
        db_bias, offset_bias = harmonics.correction_table(w)
        parts.append("/* The %s window: the parabolic refinement's bias by reported offset over 0..0.5, and its main lobe's reach in bins */" % name)
        parts.append(c_array("harmonics_golden_%s_db_bias" % name, db_bias))
        parts.append(c_array("harmonics_golden_%s_offset_bias" % name, offset_bias))
        parts.append("#define HARMONICS_GOLDEN_%s_REACH %d" % (name.upper(), harmonics.lobe_reach(w)))
    parts.append("/* kind: 0 sine, 1 sawtooth, 2 square, 3 twin, 4 noise, 5 silence; phase_deg is relative to the fundamental; thd is -1 without a fundamental */")
    parts.append("typedef struct { const char *label; int kind; double freq, freq2, amp, phase; unsigned harmonics, seed; double noise_amp;"
                 " int window; double skew; double f0; unsigned count; const int *found; const double *frequency; const double *db; const double *phase_deg; double thd; } harmonics_golden_case_t;")
    rows = []
    for label, spec, (fn, skew), f0, count in CASES:
        x = signals.generate(n=N, fs=FS, **spec)
        w = window.create(fn, N, normalize=True, skew=skew)
        bins = harmonics.spectrum((x * w).astype(np.float32))
        table = harmonics.correction_table(w)
        reach = harmonics.lobe_reach(w)
        reads = harmonics.readouts(bins, N, FS, f0, count, SEARCH, table, reach, SHOULDER, PROMINENCE)
        thd = harmonics.thd(reads)
        phase = harmonics.phases(bins, N, FS, reads)
        parts.append(c_array("harmonics_golden_%s_found" % label, [1 if r[0] else 0 for r in reads], per_line=16, ctype="int"))
        parts.append(c_array("harmonics_golden_%s_frequency" % label, [r[1] for r in reads]))
        parts.append(c_array("harmonics_golden_%s_db" % label, [r[2] for r in reads]))
        parts.append(c_array("harmonics_golden_%s_phase" % label, phase))
        rows.append("\t{ %s, %d, %s, %s, %s, %s, %u, %u, %s, %d, %s, %s, %u, harmonics_golden_%s_found, harmonics_golden_%s_frequency, harmonics_golden_%s_db, harmonics_golden_%s_phase, %s },"
                    % (c_string(label), spec["kind"], c_double(spec.get("freq", 0.0)), c_double(spec.get("freq2", 0.0)),
                       c_double(spec.get("amp", 1.0)), c_double(spec.get("phase", 0.0)), spec.get("harmonics", 10), spec.get("seed", 1),
                       c_double(spec.get("noise_amp", 0.0)), fn, c_double(skew), c_double(f0), count, label, label, label, label,
                       c_double(-1.0 if thd is None else thd)))
        for k, (found, freq, db, _) in enumerate(reads, 1):
            expected = analytic_phase(spec["kind"], k)
            note = ""
            if found and expected is not None:
                error = wrap(phase[k - 1] - expected)
                note = f"  phase {phase[k - 1]:+7.2f} (analytic {expected:+4.0f}, off {error:+.3f})"
                assert abs(error) < 0.5, f"{label} H{k}: phase {phase[k - 1]} vs analytic {expected}"
            print(f"  {label:<12} H{k:<2} {'found' if found else 'none ':5} {freq:10.3f} Hz {db:8.3f} dB{note}", file=sys.stderr)
        print(f"  {label:<12} THD {'-' if thd is None else '%.4f %%' % (100 * thd)}", file=sys.stderr)
    parts.append("static const harmonics_golden_case_t harmonics_golden_cases[] = {\n" + "\n".join(rows) + "\n};")
    parts.append("#define HARMONICS_GOLDEN_CASE_COUNT %d" % len(rows))

    out = repo_path("test", "golden", "harmonics_golden.h")
    with open(out, "w") as f:
        f.write(header("S9_TEST_HARMONICS_GOLDEN_H", "emit_harmonics_golden.py",
                       "Harmonics: per-harmonic readouts and distortion from the spectra of synthetic signals.", "\n".join(parts)))
    print(f"wrote {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
