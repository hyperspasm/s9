#!/usr/bin/env python3
"""Emit test/golden/period_golden.h: the normalized autocorrelation of a
windowed frame at every lag, period estimates for synthetic signals, peak
picking on constructed functions, and conditioner traces.

    python validation/scripts/emit_period_golden.py
"""
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
from s9 import period, signals, window  # noqa: E402
from _emit import c_array, c_double, c_string, header, repo_path  # noqa: E402

FS = 44100

# --- the normalized autocorrelation at every lag of one frame

NACF_N, NACF_LAGS = 2048, 1024
NACF_SIGNAL = dict(kind=signals.SINE, freq=200.0, amp=0.7, phase=0.3)
NACF_WINDOW = dict(function=window.HAMMING, skew=0.0, normalize=1)

# --- estimates for synthetic signals: the whole chain from samples to (period, clarity)

EST_N, LAG_MIN, LAG_MAX, TOLERANCE = 8192, 11, 2205, 0.02
EST_WINDOW = dict(function=window.BLACKMAN_HARRIS, skew=0.0, normalize=1)
EST_CASES = [
    ("sine_20",     dict(kind=signals.SINE, freq=20.0, amp=0.5, phase=0.1)),
    ("sine_55",     dict(kind=signals.SINE, freq=55.0, amp=0.5, phase=0.7)),
    ("sine_440",    dict(kind=signals.SINE, freq=440.0, amp=0.5, phase=1.1)),
    ("sine_1000",   dict(kind=signals.SINE, freq=1000.0, amp=0.5, phase=2.0)),
    ("sine_3500",   dict(kind=signals.SINE, freq=3500.0, amp=0.5, phase=0.4)),
    ("saw_110",     dict(kind=signals.SAWTOOTH, freq=110.0, amp=0.5, phase=0.2, harmonics=10)),
    ("square_220",  dict(kind=signals.SQUARE, freq=220.0, amp=0.5, phase=0.5, harmonics=9)),
    ("twin_1000_1100", dict(kind=signals.TWIN, freq=1000.0, freq2=1100.0, amp=0.4, phase=0.0)),
    ("noise",       dict(kind=signals.NOISE, seed=12345, amp=0.5)),
    ("silence",     dict(kind=signals.SILENCE)),
]

# --- peak picking on constructed functions

PICK_N = 512


def constructed(peaks, baseline=-0.2):
    """An autocorrelation-shaped array: the lag-0 lobe, then parabolic peaks of given
    (centre, height, half-width) over a negative baseline."""
    f = np.full(PICK_N, baseline)
    f[0], f[1], f[2] = 1.0, 0.5, 0.1
    for centre, height, width in peaks:
        for i in range(PICK_N):
            d = (i - centre) / width
            if abs(d) < 1.0:
                f[i] = max(f[i], baseline + (height - baseline) * (1.0 - d * d))
    return f


PICK_CASES = [
    ("first_within_tolerance", [(100.0, 0.985, 8), (200.0, 0.99, 8), (300.0, 0.98, 8)], 5, 500, TOLERANCE),
    ("weak_short_lag",   [(100.0, 0.95, 8), (200.0, 0.99, 8)], 5, 500, TOLERANCE),
    ("lag_min_excludes", [(50.0, 0.99, 8), (150.0, 0.98, 8)], 80, 500, TOLERANCE),
    ("lag_max_cuts",     [(100.0, 0.7, 8), (400.0, 0.99, 8)], 5, 300, TOLERANCE),
    ("fractional",       [(123.3, 0.97, 10)], 5, 500, TOLERANCE),
    ("cut_off_peak",     [(100.0, 0.6, 8), (500.0, 0.99, 20)], 5, 495, TOLERANCE),
    ("none",             [], 5, 500, TOLERANCE),
]

# --- conditioner traces

# frames of 1/60 s; a hold of 0.0625 s is three of them, exhausted by the
# fourth; the smoothing time constant is the one that moves halfway per frame
TRACE_DT = 1.0 / 60.0
COND = period.ConditionerConfig(gate_rms=0.01, harmonicity_on=0.9, harmonicity_off=0.8, hold_s=0.0625,
                               octave_tolerance_cents=30.0, smoothing_s=TRACE_DT / math.log(2.0), deadband_cents=5.0)
TRACES = [
    ("gate_and_hysteresis", [
        (100.0, 0.95, 0.001),   # too quiet
        (100.0, 0.85, 0.1),     # not strong enough to become valid
        (100.0, 0.95, 0.1),     # valid
        (100.0, 0.85, 0.1),     # strong enough to stay valid
        (100.0, 0.75, 0.1),     # holding 1
        (100.0, 0.75, 0.1),     # holding 2
        (100.0, 0.75, 0.1),     # holding 3
        (100.0, 0.75, 0.1),     # hold exhausted: invalid, frequency held
        (100.0, 0.95, 0.1),     # valid again
    ]),
    ("octave_fold", [
        (100.0, 0.95, 0.1),
        (50.0, 0.95, 0.1),      # an octave up: folded
        (200.0, 0.95, 0.1),     # an octave down: folded
        (52.0, 0.95, 0.1),      # not within the tolerance: followed
        (52.0, 0.95, 0.1),
    ]),
    ("octave_jump_follows", [
        (100.0, 0.95, 0.1),
        (200.0, 0.95, 0.1),     # an octave down: folded 1
        (200.0, 0.95, 0.1),     # folded 2
        (200.0, 0.95, 0.1),     # folded 3
        (200.0, 0.95, 0.1),     # past the hold: followed
        (200.0, 0.95, 0.1),
        (200.0, 0.95, 0.1),
        (200.0, 0.95, 0.1),
        (200.0, 0.95, 0.1),
    ]),
    ("two_octaves_follow", [
        (100.0, 0.95, 0.1),
        (400.0, 0.95, 0.1),     # two octaves down: halfway, to an octave from the note
        (400.0, 0.95, 0.1),     # now an octave away: folded 1
        (400.0, 0.95, 0.1),     # folded 2
        (400.0, 0.95, 0.1),     # folded 3
        (400.0, 0.95, 0.1),     # past the hold: followed
        (400.0, 0.95, 0.1),
        (400.0, 0.95, 0.1),
        (400.0, 0.95, 0.1),
        (400.0, 0.95, 0.1),
    ]),
    ("smoothing_and_deadband", [
        (100.0, 0.95, 0.1),
        (99.0, 0.95, 0.1),
        (99.0, 0.95, 0.1),
        (99.0, 0.95, 0.1),
        (99.0, 0.95, 0.1),
        (99.0, 0.95, 0.1),
        (0.0, 0.0, 0.1),        # no period: holding
        (99.0, 0.95, 0.1),
    ]),
]


def make_frame(n, spec, win):
    """(windowed frame, window)"""
    x = signals.generate(n=n, fs=FS, **spec)
    w = window.create(win["function"], n, normalize=bool(win["normalize"]), skew=win["skew"])
    return (x * w).astype(np.float32), w


def signal_fields(spec):
    return dict(kind=spec.get("kind"), freq=spec.get("freq", 0.0), freq2=spec.get("freq2", 0.0),
                amp=spec.get("amp", 1.0), phase=spec.get("phase", 0.0), harmonics=spec.get("harmonics", 10),
                seed=spec.get("seed", 1))


def main():
    parts = []

    parts.append("#define PERIOD_GOLDEN_FS %d" % FS)
    parts.append("")
    parts.append("/* A signal as the C test generates it (kind: 0 sine, 1 sawtooth, 2 square, 3 twin, 4 noise, 5 silence) */")
    parts.append("typedef struct { int kind; double freq, freq2, amp, phase; unsigned harmonics, seed; } period_golden_signal_t;")
    parts.append("/* A window as create_window_function() builds it */")
    parts.append("typedef struct { int function; double skew; int normalize; } period_golden_window_t;")
    parts.append("")

    # normalized autocorrelation
    frame, w = make_frame(NACF_N, NACF_SIGNAL, NACF_WINDOW)
    nacf = period.nacf_from_frame(frame, w)
    s = signal_fields(NACF_SIGNAL)
    parts.append("/* The normalized autocorrelation of one windowed frame at lags 0..%d */" % (NACF_LAGS - 1))
    parts.append("#define PERIOD_GOLDEN_NACF_N %d" % NACF_N)
    parts.append("#define PERIOD_GOLDEN_NACF_LAGS %d" % NACF_LAGS)
    parts.append("static const period_golden_signal_t period_golden_nacf_signal = { %d, %s, %s, %s, %s, %u, %u };"
                 % (s["kind"], c_double(s["freq"]), c_double(s["freq2"]), c_double(s["amp"]), c_double(s["phase"]), s["harmonics"], s["seed"]))
    parts.append("static const period_golden_window_t period_golden_nacf_window = { %d, %s, %d };"
                 % (NACF_WINDOW["function"], c_double(NACF_WINDOW["skew"]), NACF_WINDOW["normalize"]))
    parts.append(c_array("period_golden_nacf", nacf[:NACF_LAGS]))
    parts.append("")

    # estimates
    parts.append("/* Estimates for synthetic signals: the chain from samples to (period, clarity).")
    parts.append(" * periodic is 0 where the signal has no period; there only clarity < 0.5 is asserted. */")
    parts.append("typedef struct { const char *label; period_golden_signal_t signal; unsigned n; period_golden_window_t window;"
                 " unsigned lag_min, lag_max; double tolerance; double period, clarity; int periodic; } period_golden_estimate_t;")
    rows = []
    for label, spec in EST_CASES:
        frame, w = make_frame(EST_N, spec, EST_WINDOW)
        est_period, clarity = period.estimate(period.nacf_from_frame(frame, w), LAG_MIN, LAG_MAX, TOLERANCE)
        periodic = 1 if clarity >= 0.5 else 0
        s = signal_fields(spec)
        rows.append("\t{ %s, { %d, %s, %s, %s, %s, %u, %u }, %d, { %d, %s, %d }, %d, %d, %s, %s, %s, %d },"
                    % (c_string(label), s["kind"], c_double(s["freq"]), c_double(s["freq2"]), c_double(s["amp"]), c_double(s["phase"]),
                       s["harmonics"], s["seed"], EST_N, EST_WINDOW["function"], c_double(EST_WINDOW["skew"]), EST_WINDOW["normalize"],
                       LAG_MIN, LAG_MAX, c_double(TOLERANCE), c_double(est_period), c_double(clarity), periodic))
        print(f"  estimate {label:<16} period {est_period:10.4f}  clarity {clarity:.4f}  freq {FS / est_period if est_period else 0:9.3f}", file=sys.stderr)
    parts.append("static const period_golden_estimate_t period_golden_estimates[] = {\n" + "\n".join(rows) + "\n};")
    parts.append("#define PERIOD_GOLDEN_ESTIMATE_COUNT %d" % len(rows))
    parts.append("")

    # picking
    parts.append("/* Peak picking on constructed functions */")
    parts.append("#define PERIOD_GOLDEN_PICK_N %d" % PICK_N)
    parts.append("typedef struct { const char *label; const double *nacf; unsigned lag_min, lag_max; double tolerance; double period, clarity; int periodic; } period_golden_pick_t;")
    rows = []
    for label, peaks, lag_min, lag_max, tolerance in PICK_CASES:
        f = constructed(peaks)
        est_period, clarity = period.estimate(f, lag_min, lag_max, tolerance)
        parts.append(c_array("period_golden_pick_%s" % label, f, per_line=8))
        rows.append("\t{ %s, period_golden_pick_%s, %d, %d, %s, %s, %s, %d },"
                    % (c_string(label), label, lag_min, lag_max, c_double(tolerance), c_double(est_period), c_double(clarity), 1 if est_period > 0 else 0))
        print(f"  pick {label:<18} period {est_period:8.3f}  clarity {clarity:.4f}", file=sys.stderr)
    parts.append("static const period_golden_pick_t period_golden_picks[] = {\n" + "\n".join(rows) + "\n};")
    parts.append("#define PERIOD_GOLDEN_PICK_COUNT %d" % len(rows))
    parts.append("")

    # conditioner
    parts.append("/* Conditioner traces: the outputs frame by frame for a configuration, frames PERIOD_GOLDEN_TRACE_DT seconds apart */")
    parts.append("#define PERIOD_GOLDEN_TRACE_DT %s" % c_double(TRACE_DT))
    parts.append("typedef struct { double gate_rms, harmonicity_on, harmonicity_off, hold_s, octave_tolerance_cents, smoothing_s, deadband_cents; } period_golden_conditioner_config_t;")
    parts.append("static const period_golden_conditioner_config_t period_golden_conditioner_config = { %s, %s, %s, %s, %s, %s, %s };"
                 % (c_double(COND.gate_rms), c_double(COND.harmonicity_on), c_double(COND.harmonicity_off), c_double(COND.hold_s),
                    c_double(COND.octave_tolerance_cents), c_double(COND.smoothing_s), c_double(COND.deadband_cents)))
    parts.append("typedef struct { double period, harmonicity, rms; int valid; double frequency; } period_golden_step_t;")
    parts.append("typedef struct { const char *label; const period_golden_step_t *steps; unsigned count; } period_golden_trace_t;")
    rows = []
    for label, steps in TRACES:
        cond = period.Conditioner(COND)
        lines = []
        for step_period, harmonicity, rms in steps:
            valid, freq = cond.update(step_period, harmonicity, rms, FS, TRACE_DT)
            lines.append("\t{ %s, %s, %s, %d, %s }," % (c_double(step_period), c_double(harmonicity), c_double(rms), 1 if valid else 0, c_double(freq)))
            print(f"  trace {label:<24} in ({step_period:6.1f}, {harmonicity:.2f}, {rms:.3f}) -> valid {int(valid)} freq {freq:9.4f}", file=sys.stderr)
        parts.append("static const period_golden_step_t period_golden_trace_%s[] = {\n%s\n};" % (label, "\n".join(lines)))
        rows.append("\t{ %s, period_golden_trace_%s, %d }," % (c_string(label), label, len(steps)))
    parts.append("static const period_golden_trace_t period_golden_traces[] = {\n" + "\n".join(rows) + "\n};")
    parts.append("#define PERIOD_GOLDEN_TRACE_COUNT %d" % len(rows))

    out = repo_path("test", "golden", "period_golden.h")
    with open(out, "w") as f:
        f.write(header("S9_TEST_PERIOD_GOLDEN_H", "emit_period_golden.py",
                       "Period: the normalized autocorrelation of a frame, estimates for synthetic signals, peak picking, and conditioner traces.",
                       "\n".join(parts)))
    print(f"wrote {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
