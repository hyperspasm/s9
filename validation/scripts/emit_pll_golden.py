#!/usr/bin/env python3
"""Emit test/golden/pll_golden.h: the loop's outputs observation by
observation for event trains that jitter, that start off the seeded period
by a little and by a lot, that slip, that change rate, that skip events,
and that are passed straight through.

    python validation/scripts/emit_pll_golden.py
"""
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
from s9 import pll  # noqa: E402
from _emit import c_double, c_string, header, repo_path  # noqa: E402


def jitter(seed, count, spread):
    """Deterministic jitter in [-spread, spread) from a linear congruential generator."""
    out, s = [], seed & 0xFFFFFFFF
    for _ in range(count):
        s = (1664525 * s + 1013904223) & 0xFFFFFFFF
        out.append(spread * ((s >> 8) * (2.0 / 16777216.0) - 1.0))
    return out


def train(start, period, count, jitters=None, jumps=None):
    """Event times start + k * period, plus per-event jitter, plus jumps: {k: shift} from event k on."""
    times, shift = [], 0.0
    for k in range(count):
        if jumps and k in jumps:
            shift += jumps[k]
        times.append(start + k * period + (jitters[k] if jitters else 0.0) + shift)
    return times


TRACES = [
    # (label, config, seed period, event times)
    ("steady_jitter", pll.Config(300.0, 0.25), 100.0, train(1000.0, 100.0, 60, jitter(7, 60, 6.0))),
    ("period_off",    pll.Config(300.0, 0.25), 105.0, train(1000.0, 100.0, 60)),
    ("period_far_off", pll.Config(300.0, 0.25), 130.0, train(1000.0, 100.0, 60)),
    ("new_rate",      pll.Config(300.0, 0.25), 100.0, train(1000.0, 100.0, 30) + train(1000.0 + 29 * 100.0 + 110.0, 110.0, 30)),
    ("slip",          pll.Config(300.0, 0.25), 100.0, train(1000.0, 100.0, 30, jumps={15: 40.0})),
    ("missed_events", pll.Config(300.0, 0.25), 100.0, [1000.0 + k * 100.0 for k in (0, 1, 2, 5, 6, 9, 10, 11)]),
    ("straight_through", pll.Config(0.0, 0.25), 100.0, train(1000.0, 100.0, 12, jitter(3, 12, 6.0))),
]


def main():
    parts = []
    parts.append("typedef struct { double tc, capture; } pll_golden_config_t;")
    parts.append("/* One observation: the time observed; the error returned, the period after it, and the next event predicted at or after it */")
    parts.append("typedef struct { double t, error, period, next; } pll_golden_step_t;")
    parts.append("typedef struct { const char *label; pll_golden_config_t config; double seed; const pll_golden_step_t *steps; unsigned count; } pll_golden_trace_t;")
    rows = []
    for label, config, seed, times in TRACES:
        loop = pll.Pll(config)
        loop.seed(seed)
        lines = []
        errors = []
        for t in times:
            error = loop.observe(t)
            nxt = loop.next(t)
            errors.append(error)
            lines.append("\t{ %s, %s, %s, %s }," % (c_double(t), c_double(error), c_double(loop.period), c_double(nxt)))
        parts.append("static const pll_golden_step_t pll_golden_trace_%s[] = {\n%s\n};" % (label, "\n".join(lines)))
        rows.append("\t{ %s, { %s, %s }, %s, pll_golden_trace_%s, %d },"
                    % (c_string(label), c_double(config.tc), c_double(config.capture), c_double(seed), label, len(times)))
        tail = errors[len(errors) // 2:]
        # how steady the predictions were: the spread of the prediction times about the true grid, where the train has one
        print(f"  {label:<18} period {loop.period:9.4f}  |error| over the last half: mean {sum(abs(e) for e in tail) / len(tail):7.3f}  max {max(abs(e) for e in tail):7.3f}", file=sys.stderr)
    parts.append("static const pll_golden_trace_t pll_golden_traces[] = {\n" + "\n".join(rows) + "\n};")
    parts.append("#define PLL_GOLDEN_TRACE_COUNT %d" % len(rows))

    out = repo_path("test", "golden", "pll_golden.h")
    with open(out, "w") as f:
        f.write(header("S9_TEST_PLL_GOLDEN_H", "emit_pll_golden.py",
                       "The phase-locked loop's outputs, observation by observation, for event trains.", "\n".join(parts)))
    print(f"wrote {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
