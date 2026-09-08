#!/usr/bin/env python3
"""Emit test/golden/harmonics_golden.h: per-harmonic readouts from the spectra
of synthetic signals.

    python validation/scripts/emit_harmonics_golden.py
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
from s9 import harmonics, signals, window  # noqa: E402
from _emit import c_array, c_double, c_string, header, repo_path  # noqa: E402

FS, N, SEARCH = 44100, 4096, 3

# (label, signal, window, reference f0, harmonics to read)
CASES = [
    ("saw_220",   dict(kind=signals.SAWTOOTH, freq=220.0, amp=0.5, phase=0.2, harmonics=10), (window.HAMMING, 0.0), 220.0, 12),
    ("square_300", dict(kind=signals.SQUARE, freq=300.0, amp=0.5, phase=0.5, harmonics=9), (window.HAMMING, 0.0), 300.0, 8),
    ("sine_5000",  dict(kind=signals.SINE, freq=5000.0, amp=1.0, phase=0.0), (window.HAMMING, 0.0), 5000.0, 6),
    ("sine_offbin", dict(kind=signals.SINE, freq=440.7, amp=0.25, phase=0.9), (window.HAMMING, 0.0), 440.7, 3),
    ("saw_110_bh", dict(kind=signals.SAWTOOTH, freq=110.0, amp=0.5, phase=0.0, harmonics=16), (window.BLACKMAN_HARRIS, 0.0), 110.0, 16),
]


def main():
    parts = []
    parts.append("#define HARMONICS_GOLDEN_FS %d" % FS)
    parts.append("#define HARMONICS_GOLDEN_N %d" % N)
    parts.append("#define HARMONICS_GOLDEN_SEARCH %d" % SEARCH)
    parts.append("#define HARMONICS_GOLDEN_CORRECTION_POINTS %d" % harmonics.CORRECTION_POINTS)
    for fn, name in ((window.HAMMING, "hamming"), (window.BLACKMAN_HARRIS, "blackman_harris")):
        db_bias, offset_bias = harmonics.correction_table(window.create(fn, N, normalize=True, skew=0.0))
        parts.append("/* Parabolic refinement bias for the %s window, by reported offset over 0..0.5 */" % name)
        parts.append(c_array("harmonics_golden_%s_db_bias" % name, db_bias))
        parts.append(c_array("harmonics_golden_%s_offset_bias" % name, offset_bias))
    parts.append("/* kind: 0 sine, 1 sawtooth, 2 square, 3 twin, 4 noise, 5 silence */")
    parts.append("typedef struct { const char *label; int kind; double freq, freq2, amp, phase; unsigned harmonics, seed;"
                 " int window; double skew; double f0; unsigned count; const int *found; const double *frequency; const double *db; } harmonics_golden_case_t;")
    rows = []
    for label, spec, (fn, skew), f0, count in CASES:
        x = signals.generate(n=N, fs=FS, **spec)
        w = window.create(fn, N, normalize=True, skew=skew)
        bins = harmonics.spectrum((x * w).astype(np.float32))
        table = harmonics.correction_table(w)
        reads = harmonics.readouts(bins, N, FS, f0, count, SEARCH, table)
        parts.append(c_array("harmonics_golden_%s_found" % label, [1 if r[0] else 0 for r in reads], per_line=16, ctype="int"))
        parts.append(c_array("harmonics_golden_%s_frequency" % label, [r[1] for r in reads]))
        parts.append(c_array("harmonics_golden_%s_db" % label, [r[2] for r in reads]))
        rows.append("\t{ %s, %d, %s, %s, %s, %s, %u, %u, %d, %s, %s, %u, harmonics_golden_%s_found, harmonics_golden_%s_frequency, harmonics_golden_%s_db },"
                    % (c_string(label), spec["kind"], c_double(spec.get("freq", 0.0)), c_double(spec.get("freq2", 0.0)),
                       c_double(spec.get("amp", 1.0)), c_double(spec.get("phase", 0.0)), spec.get("harmonics", 10), spec.get("seed", 1),
                       fn, c_double(skew), c_double(f0), count, label, label, label))
        for k, (found, freq, db) in enumerate(reads, 1):
            print(f"  {label:<12} H{k:<2} {'found' if found else 'none ':5} {freq:10.3f} Hz {db:8.3f} dB", file=sys.stderr)
    parts.append("static const harmonics_golden_case_t harmonics_golden_cases[] = {\n" + "\n".join(rows) + "\n};")
    parts.append("#define HARMONICS_GOLDEN_CASE_COUNT %d" % len(rows))

    out = repo_path("test", "golden", "harmonics_golden.h")
    with open(out, "w") as f:
        f.write(header("S9_TEST_HARMONICS_GOLDEN_H", "emit_harmonics_golden.py",
                       "Harmonics: per-harmonic readouts from the spectra of synthetic signals.", "\n".join(parts)))
    print(f"wrote {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
