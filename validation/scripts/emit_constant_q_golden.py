#!/usr/bin/env python3
"""Emit test/golden/constant_q_golden.h: the bandwidth of every window function, the
window length and the readouts of each column, and the level the constant-Q readout's
definition gives at each column for frames holding a sine on a column and between
columns, sines at the bottom, the middle and the top of the range, two tones a
semitone apart, impulses at the frame's centre and away from it, noise, a sine in
noise, and silence.

    python validation/scripts/emit_constant_q_golden.py
"""
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
import numpy as np  # noqa: E402

from s9 import constant_q, signals, window  # noqa: E402
from _emit import c_array, c_double, c_string, header, repo_path  # noqa: E402

FS, N, MIN_WINDOW, RESOLUTION, THRESHOLD_DB, READOUTS_PER_RIDGE = 48000, 32768, 960, 1.0, -100.0, 3.0
F_LO, F_HI, COLUMNS = 20.0, 20000.0, 64
FLOOR_DB = -95.0   # what the kernels' truncation leaves out stays below this, measure_constant_q.py checks
CENTRE = N // 2
SEMITONE = 2.0 ** (1.0 / 12.0)

KIND_NAMES = {signals.SINE: "SIGNAL_SINE", signals.TWIN: "SIGNAL_TWIN", signals.NOISE: "SIGNAL_NOISE",
              signals.SILENCE: "SIGNAL_SILENCE", signals.IMPULSE: "SIGNAL_IMPULSE"}


def cases(edges):
    centre = lambda c: math.sqrt(edges[c] * edges[c + 1])   # noqa: E731
    return [
        # label, kind, freq, freq2, amp, phase, index, seed, noise_amp
        ("sine_on_column", signals.SINE, centre(32), 0.0, 0.5, 0.3, 0, 0, 0.0),
        ("sine_between_columns", signals.SINE, edges[41] * 1.0001, 0.0, 1.0, 1.0, 0, 0, 0.0),
        ("sine_30", signals.SINE, 30.0, 0.0, 1.0, 0.0, 0, 0, 0.0),
        ("sine_1000_full_scale", signals.SINE, 1000.0, 0.0, 1.0, 0.5, 0, 0, 0.0),
        ("sine_12000", signals.SINE, 12000.0, 0.0, 0.25, 2.0, 0, 0, 0.0),
        ("twin_semitone_200", signals.TWIN, 200.0, 200.0 * SEMITONE, 0.5, 0.0, 0, 0, 0.0),
        ("twin_semitone_3000", signals.TWIN, 3000.0, 3000.0 * SEMITONE, 0.5, 0.0, 0, 0, 0.0),
        ("impulse_centre", signals.IMPULSE, 0.0, 0.0, 1.0, 0.0, CENTRE, 0, 0.0),
        ("impulse_50ms_late", signals.IMPULSE, 0.0, 0.0, 1.0, 0.0, CENTRE + 2400, 0, 0.0),
        ("impulse_300ms_early", signals.IMPULSE, 0.0, 0.0, 1.0, 0.0, CENTRE - 14400, 0, 0.0),
        ("noise", signals.NOISE, 0.0, 0.0, 0.1, 0.0, 0, 7, 0.0),
        ("sine_in_noise", signals.SINE, 440.0, 0.0, 0.1, 0.0, 0, 3, 0.01),
        ("silence", signals.SILENCE, 0.0, 0.0, 0.0, 0.0, 0, 0, 0.0),
    ]


def main():
    cfg = constant_q.Config(fs=FS, n=N, min_window=MIN_WINDOW, resolution=RESOLUTION, function=window.BLACKMAN_HARRIS,
                            threshold_db=THRESHOLD_DB, readouts_per_ridge=READOUTS_PER_RIDGE)
    edges = constant_q.edges(F_LO, F_HI, COLUMNS)
    bands = [constant_q.band(cfg, edges[c], edges[c + 1]) for c in range(COLUMNS)]
    parts = []
    parts.append("#define CONSTANT_Q_GOLDEN_FS %d" % FS)
    parts.append("#define CONSTANT_Q_GOLDEN_N %d" % N)
    parts.append("#define CONSTANT_Q_GOLDEN_MIN_WINDOW %d" % MIN_WINDOW)
    parts.append("#define CONSTANT_Q_GOLDEN_RESOLUTION %s" % c_double(RESOLUTION))
    parts.append("#define CONSTANT_Q_GOLDEN_WINDOW_FUNCTION WINDOW_FUNCTION_BLACKMAN_HARRIS")
    parts.append("#define CONSTANT_Q_GOLDEN_THRESHOLD_DB %s" % c_double(THRESHOLD_DB))
    parts.append("#define CONSTANT_Q_GOLDEN_READOUTS_PER_RIDGE %s" % c_double(READOUTS_PER_RIDGE))
    parts.append("#define CONSTANT_Q_GOLDEN_F_LO %s" % c_double(F_LO))
    parts.append("#define CONSTANT_Q_GOLDEN_F_HI %s" % c_double(F_HI))
    parts.append("#define CONSTANT_Q_GOLDEN_COLUMNS %d" % COLUMNS)
    parts.append("/* Readouts are held to the definition within this, in amplitude relative to full scale: the residue of the kernels' truncation */")
    parts.append("#define CONSTANT_Q_GOLDEN_FLOOR_DB %s" % c_double(FLOOR_DB))
    parts.append("/* The bandwidth of each window function at half amplitude, in bins, in window_function_t order */")
    parts.append(c_array("constant_q_golden_bandwidths", [window.bandwidth(f) for f in range(len(window.NAMES))]))
    parts.append("/* The window length of each column, and how many readouts it has */")
    parts.append(c_array("constant_q_golden_windows", [length for length, _ in bands], per_line=8, ctype="unsigned"))
    parts.append(c_array("constant_q_golden_readouts", [len(freqs) for _, freqs in bands], per_line=8, ctype="unsigned"))
    parts.append("/* A case: a test signal by kind, its impulse at index when it is one, and the level of each column, "
                 "the definition's, in dB relative to a full-scale sine, %g where the definition is exactly silent */" % constant_q.db(np.array([0.0]))[0])
    parts.append("typedef struct { const char *label; int kind; double freq, freq2, amp, phase; unsigned index, seed; double noise_amp;"
                 " const double *db; } constant_q_golden_case_t;")
    rows = []
    for label, kind, freq, freq2, amp, phase, index, seed, noise_amp in cases(edges):
        x = signals.generate(kind, N, FS, freq=freq, freq2=freq2, amp=amp, phase=phase, seed=seed, noise_amp=noise_amp, index=index)
        levels = constant_q.db(constant_q.transform(x, cfg, edges))
        parts.append(c_array("constant_q_golden_%s_db" % label, list(levels), per_line=8))
        rows.append("\t{ %s, %s, %s, %s, %s, %s, %u, %u, %s, constant_q_golden_%s_db },"
                    % (c_string(label), KIND_NAMES[kind], c_double(freq), c_double(freq2), c_double(amp), c_double(phase), index, seed,
                       c_double(noise_amp), label))
        peak = int(levels.argmax())
        print(f"  {label:<22} peak {levels.max():8.2f} dB at column {peak:2d} ({edges[peak]:8.1f} to {edges[peak + 1]:8.1f} Hz)", file=sys.stderr)
    parts.append("static const constant_q_golden_case_t constant_q_golden_cases[] = {\n" + "\n".join(rows) + "\n};")
    parts.append("#define CONSTANT_Q_GOLDEN_CASE_COUNT %d" % len(rows))

    out = repo_path("test", "golden", "constant_q_golden.h")
    with open(out, "w") as f:
        f.write(header("S9_TEST_CONSTANT_Q_GOLDEN_H", "emit_constant_q_golden.py",
                       "The constant-Q readout's definition at each column for synthetic frames.", "\n".join(parts)))
    print(f"wrote {out}: {constant_q.readouts(constant_q.kernels(cfg, edges))} readouts over {COLUMNS} columns", file=sys.stderr)


if __name__ == "__main__":
    main()
