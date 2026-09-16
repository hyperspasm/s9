#!/usr/bin/env python3
"""Emit test/golden/conditioner_golden.h: the conditioner's outputs frame by frame for
traces of estimates that exercise the gate and its hysteresis, the hold, the octave
guard, the smoothing with its dead band, and the snap to a new note.

    python validation/scripts/emit_conditioner_golden.py
"""
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
from s9 import conditioner  # noqa: E402
from _emit import c_double, c_string, header, repo_path  # noqa: E402

# frames of 1/60 s; a hold of 0.0625 s is three of them, exhausted by the
# fourth; the smoothing time constant is the one that moves halfway per frame
TRACE_DT = 1.0 / 60.0
CONFIG = conditioner.Config(gate_rms=0.01, harmonicity_on=0.9, harmonicity_off=0.8, hold_s=0.0625,
                            octave_tolerance_cents=30.0, smoothing_s=TRACE_DT / math.log(2.0), deadband_cents=5.0,
                            snap_cents=50.0)

# (label, steps of (estimate Hz, harmonicity, rms))
TRACES = [
    ("gate_and_hysteresis", [
        (440.0, 0.95, 0.001),   # too quiet
        (440.0, 0.85, 0.1),     # not enough to become valid
        (440.0, 0.95, 0.1),     # valid
        (440.0, 0.85, 0.1),     # enough to stay valid
        (440.0, 0.75, 0.1),     # holding 1
        (440.0, 0.75, 0.1),     # holding 2
        (440.0, 0.75, 0.1),     # holding 3
        (440.0, 0.75, 0.1),     # hold exhausted: invalid, frequency held
        (440.0, 0.95, 0.1),     # valid again
    ]),
    ("octave_fold", [
        (440.0, 0.95, 0.1),
        (880.0, 0.95, 0.1),     # an octave up: folded
        (220.0, 0.95, 0.1),     # an octave down: folded
        (850.0, 0.95, 0.1),     # not within the tolerance of an octave: a new note, snapped to
        (850.0, 0.95, 0.1),
    ]),
    ("octave_jump_follows", [
        (440.0, 0.95, 0.1),
        (220.0, 0.95, 0.1),     # an octave down: folded 1
        (220.0, 0.95, 0.1),     # folded 2
        (220.0, 0.95, 0.1),     # folded 3
        (220.0, 0.95, 0.1),     # past the hold: a new note, snapped to
        (220.0, 0.95, 0.1),
        (220.0, 0.95, 0.1),
    ]),
    ("two_octaves_snap", [
        (440.0, 0.95, 0.1),
        (110.0, 0.95, 0.1),     # two octaves down: no octave to fold, a new note, snapped to
        (110.0, 0.95, 0.1),
        (110.0, 0.95, 0.1),
    ]),
    ("smoothing_and_deadband", [
        (440.0, 0.95, 0.1),
        (436.0, 0.95, 0.1),     # 16 cents away: smoothed toward, halfway each frame
        (436.0, 0.95, 0.1),
        (436.0, 0.95, 0.1),
        (436.0, 0.95, 0.1),
        (436.0, 0.95, 0.1),
        (0.0, 0.0, 0.1),        # no estimate: holding
        (436.0, 0.95, 0.1),
    ]),
    ("snap", [
        (440.0, 0.95, 0.1),
        (458.0, 0.95, 0.1),     # 69 cents away: snapped to
        (458.0, 0.95, 0.1),
    ]),
]


def main():
    parts = []
    parts.append("/* Traces: the outputs frame by frame for a configuration, frames CONDITIONER_GOLDEN_TRACE_DT seconds apart */")
    parts.append("#define CONDITIONER_GOLDEN_TRACE_DT %s" % c_double(TRACE_DT))
    parts.append("typedef struct { double gate_rms, harmonicity_on, harmonicity_off, hold_s, octave_tolerance_cents, smoothing_s, deadband_cents, snap_cents; }"
                 " conditioner_golden_config_t;")
    parts.append("static const conditioner_golden_config_t conditioner_golden_config = { %s, %s, %s, %s, %s, %s, %s, %s };"
                 % (c_double(CONFIG.gate_rms), c_double(CONFIG.harmonicity_on), c_double(CONFIG.harmonicity_off), c_double(CONFIG.hold_s),
                    c_double(CONFIG.octave_tolerance_cents), c_double(CONFIG.smoothing_s), c_double(CONFIG.deadband_cents), c_double(CONFIG.snap_cents)))
    parts.append("/* One frame: the estimate in Hz, its harmonicity, the frame's RMS; then whether the fundamental is valid, and its frequency */")
    parts.append("typedef struct { double estimate_hz, harmonicity, rms; int valid; double frequency; } conditioner_golden_step_t;")
    parts.append("typedef struct { const char *label; const conditioner_golden_step_t *steps; unsigned count; } conditioner_golden_trace_t;")
    rows = []
    for label, steps in TRACES:
        cond = conditioner.Conditioner(CONFIG)
        lines = []
        for estimate_hz, harmonicity, rms in steps:
            valid, freq = cond.update(estimate_hz, harmonicity, rms, TRACE_DT)
            lines.append("\t{ %s, %s, %s, %d, %s }," % (c_double(estimate_hz), c_double(harmonicity), c_double(rms), 1 if valid else 0, c_double(freq)))
            print(f"  trace {label:<24} in ({estimate_hz:6.1f}, {harmonicity:.2f}, {rms:.3f}) -> valid {int(valid)} freq {freq:9.4f}", file=sys.stderr)
        parts.append("static const conditioner_golden_step_t conditioner_golden_trace_%s[] = {\n%s\n};" % (label, "\n".join(lines)))
        rows.append("\t{ %s, conditioner_golden_trace_%s, %d }," % (c_string(label), label, len(steps)))
    parts.append("static const conditioner_golden_trace_t conditioner_golden_traces[] = {\n" + "\n".join(rows) + "\n};")
    parts.append("#define CONDITIONER_GOLDEN_TRACE_COUNT %d" % len(rows))

    out = repo_path("test", "golden", "conditioner_golden.h")
    with open(out, "w") as f:
        f.write(header("S9_TEST_CONDITIONER_GOLDEN_H", "emit_conditioner_golden.py",
                       "The conditioner's outputs frame by frame for traces of estimates.", "\n".join(parts)))
    print(f"wrote {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
