#!/usr/bin/env python3
"""Measure the harmonic series estimator: on synthetic signals built to trouble it, at
fundamentals across the range, against what each is expected to give; and, with
--vectors DIR, on a directory of WAV files with a manifest.csv naming each file's
fundamental (columns file, f0_hz), at four positions in each file.

    python validation/scripts/measure_harmonic_series.py [--vectors DIR] [--quiet]

Exits nonzero when an expectation fails. Cases marked "report" are limits documented
rather than promised.
"""
import argparse
import csv
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
from s9 import harmonic_series, harmonics, recipes, signals, window  # noqa: E402

FS = 48000
N = 32768            # the transform of a 0.75 s window at 48 kHz
AMP = 0.2
F0S = (25.0, 40.0, 55.0, 110.0, 220.0, 440.0, 880.0, 1760.0, 3500.0)
VALID = 0.5          # harmonicity at which a frame counts as a series


def db_of(x, w):
    return harmonics.magnitude_db(harmonics.spectrum((x * w).astype(np.float32)), len(x))


def cents(a, b):
    return 1200.0 * np.log2(a / b)


# --- the synthetic cases: label, a function of f0 giving (table, noise_amp), expectation, tolerance in cents

def case_list():
    def vowel(f0):
        return recipes.vowel(f0, FS), 0.0

    def h1_absent(f0):
        return recipes.without(recipes.vowel(f0, FS), f0, (1,)), 0.0

    def h1_h2_absent(f0):
        return recipes.without(recipes.vowel(f0, FS), f0, (1, 2)), 0.0

    def odd(f0):
        return recipes.odd_only(recipes.vowel(f0, FS), f0), 0.0

    def noise20(f0):
        return recipes.vowel(f0, FS), AMP * 10.0 ** (-20.0 / 20.0) * np.sqrt(3.0)

    def noise6(f0):
        return recipes.vowel(f0, FS), AMP * 10.0 ** (-6.0 / 20.0) * np.sqrt(3.0)

    def aliases25(f0):
        return recipes.with_aliases(recipes.vowel(f0, FS), f0, FS, 44100.0, -25.0), 0.0

    def aliases15(f0):
        return recipes.with_aliases(recipes.vowel(f0, FS), f0, FS, 44100.0, -15.0), 0.0

    def hum(f0):
        return recipes.with_hum(recipes.vowel(f0, FS), 60.0, -40.0), 0.0

    def sub6(f0):
        return recipes.sub_oscillator(recipes.vowel(f0, FS), f0, -6.0, FS), 0.0

    def sub30(f0):
        return recipes.sub_oscillator(recipes.vowel(f0, FS), f0, -30.0, FS), 0.0

    def sub15(f0):
        return recipes.sub_oscillator(recipes.vowel(f0, FS), f0, -15.0, FS), 0.0

    def saw(f0):
        return recipes.sawtooth(f0, FS), 0.0

    def sine(f0):
        return recipes.sine(f0), 0.0

    def string(f0):
        return recipes.stretched(f0, FS, 0.0004), 0.0

    def unison(f0):
        return recipes.detuned(recipes.vowel(f0, FS), 5.0, FS), 0.0

    def octave_pair(f0):
        return recipes.detuned(recipes.vowel(f0, FS), 1200.0, FS, -3.0), 0.0

    def fifth(f0):
        return recipes.detuned(recipes.vowel(f0, FS), 701.955, FS, 0.0), 0.0

    return [
        ("vowel", vowel, "f0", 0.01),
        ("vowel, H1 absent", h1_absent, "f0", 0.01),
        ("vowel, H1 and H2 absent", h1_h2_absent, "f0", 0.01),
        ("vowel, odd harmonics only", odd, "f0", 0.01),
        ("sawtooth", saw, "f0", 0.01),
        ("sine", sine, "f0", 0.01),
        ("vowel, noise 20 dB below", noise20, "f0", 0.05),
        ("vowel, noise 6 dB below", noise6, "f0", 0.5),
        ("vowel, aliases at -25 dB", aliases25, "f0", 0.01),
        ("vowel, hum at -40 dB", hum, "f0", 0.01),
        ("vowel, sub-oscillator at -6 dB", sub6, "f0/2 in range", 0.01),
        ("vowel, sub-oscillator at -30 dB", sub30, "f0", 0.01),
        ("vowel, octave above at -3 dB", octave_pair, "f0", 0.01),
        ("vowel, aliases at -15 dB", aliases15, "report", 0.0),
        ("vowel, sub-oscillator at -15 dB", sub15, "report", 0.0),
        ("string, B = 0.0004", string, "report", 0.0),
        ("unison 5 cents apart", unison, "report", 0.0),
        ("two vowels a fifth apart", fifth, "report", 0.0),
    ]


def unsteady_cases():
    """Time-varying signals, made here rather than from tables."""
    t = np.arange(N) / FS

    def modulated(f0, vibrato_cents=0.0, rate=5.0, tremolo=0.0):
        table = recipes.vowel(f0, FS)
        depth = 2.0 ** (vibrato_cents / 1200.0) - 1.0
        # the phase of a frequency modulated by a fraction `depth` at `rate`
        phase = 2.0 * np.pi * f0 * (t + depth / (2.0 * np.pi * rate) * np.sin(2.0 * np.pi * rate * t))
        x = np.zeros(N)
        for f, a, p in table:
            x += AMP * a * np.sin(round(f / f0) * phase + p)
        if tremolo:
            x *= 1.0 + tremolo * np.sin(2.0 * np.pi * 4.0 * t)
        return x.astype(np.float32)

    return [
        ("vowel, tremolo 50% at 4 Hz", lambda f0: modulated(f0, tremolo=0.5), "f0", 0.3),
        ("vowel, vibrato 1 cent at 5 Hz", lambda f0: modulated(f0, 1.0), "f0", 0.05),
        ("vowel, vibrato 20 cents at 5 Hz", lambda f0: modulated(f0, 20.0), "flag or mean", 0.5),
    ]


def judge(expectation, tol, f0, est):
    """(passed, note) for an estimate against an expectation."""
    if expectation == "report":
        return True, ""
    flagged = est.harmonicity < VALID or est.f0 == 0.0
    if expectation == "flag":
        return flagged, "" if flagged else "not flagged"
    if expectation == "flag or mean" and flagged:
        return True, ""
    if expectation == "f0/2 in range":
        expectation = "f0/2" if f0 / 2.0 >= harmonic_series.Config().f_min else "f0"
    target = f0 / 2.0 if expectation == "f0/2" else f0
    if est.f0 <= 0.0:
        return False, "no estimate"
    err = cents(est.f0, target)
    if abs(err) > tol:
        return False, f"off by {err:+.4f} cents"
    if est.harmonicity < VALID:
        return False, f"harmonicity {est.harmonicity:.2f}"
    return True, ""


def run_synthetic(quiet):
    w = window.create(window.BLACKMAN_HARRIS, N)
    reach, table = harmonics.lobe_reach(w), harmonics.correction_table(w)
    cfg = harmonic_series.Config()
    failures = 0
    print(f"{'case':<34} {'f0':>7} {'cents':>10} {'harmon':>7} {'count':>5} {'±cents':>8}  result")
    for label, make, expectation, tol in case_list():
        for f0 in F0S:
            partial_table, noise_amp = make(f0)
            x = signals.generate(signals.PARTIALS, N, FS, amp=AMP, table=partial_table, seed=int(f0) + 1, noise_amp=noise_amp)
            est = harmonic_series.estimate(db_of(x, w), FS / N, reach, table, cfg)
            ok, note = judge(expectation, tol, f0, est)
            failures += 0 if ok else 1
            err = cents(est.f0, f0) if est.f0 > 0 else float("nan")
            if not quiet or not ok or expectation == "report":
                print(f"{label:<34} {f0:7.1f} {err:+10.4f} {est.harmonicity:7.3f} {est.count:5d} {est.uncertainty_cents:8.4f}  "
                      f"{'ok' if ok else 'FAIL'} {note}{'  (report)' if expectation == 'report' else ''}")
    for label, make, expectation, tol in unsteady_cases():
        for f0 in F0S:
            x = make(f0)
            est = harmonic_series.estimate(db_of(x, w), FS / N, reach, table, cfg)
            ok, note = judge(expectation, tol, f0, est)
            failures += 0 if ok else 1
            err = cents(est.f0, f0) if est.f0 > 0 else float("nan")
            if not quiet or not ok or expectation == "report":
                print(f"{label:<34} {f0:7.1f} {err:+10.4f} {est.harmonicity:7.3f} {est.count:5d} {est.uncertainty_cents:8.4f}  "
                      f"{'ok' if ok else 'FAIL'} {note}{'  (report)' if expectation == 'report' else ''}")
    # nothing to find
    for label, x in (("noise", signals.generate(signals.NOISE, N, FS, amp=AMP, seed=9)), ("silence", signals.silence(N))):
        est = harmonic_series.estimate(db_of(x, w), FS / N, reach, table, cfg)
        ok = est.f0 == 0.0 or est.harmonicity < VALID
        failures += 0 if ok else 1
        print(f"{label:<34} {'':>7} {'':>10} {est.harmonicity:7.3f} {est.count:5d} {'':>8}  {'ok' if ok else 'FAIL'}")
    return failures


def run_vectors(directory, quiet):
    rows = list(csv.DictReader(open(os.path.join(directory, "manifest.csv"))))
    cfg = harmonic_series.Config()
    worst, wrong, cache = 0.0, 0, {}
    errors = []
    for r in rows:
        wv = wave.open(os.path.join(directory, r["file"]))
        fs = wv.getframerate()
        x = np.frombuffer(wv.readframes(wv.getnframes()), dtype="<i2").astype(np.float64) / 32768.0
        if wv.getnchannels() != 1:
            x = x[::wv.getnchannels()]
        if fs not in cache:
            w = window.create(window.BLACKMAN_HARRIS, N)
            cache[fs] = (w, harmonics.lobe_reach(w), harmonics.correction_table(w))
        w, reach, table = cache[fs]
        f0 = float(r["f0_hz"])
        ests = []
        for start in (int(0.5 * fs), int(1.5 * fs), int(2.5 * fs), int(3.5 * fs)):
            frame = x[start:start + N].astype(np.float32)
            if len(frame) < N:
                break
            ests.append(harmonic_series.estimate(db_of(frame, w), fs / N, reach, table, cfg))
        errs = [cents(e.f0, f0) if e.f0 > 0 else float("nan") for e in ests]
        octave_ok = all(abs(e) < 50 for e in errs if not np.isnan(e)) and not any(np.isnan(e) for e in errs)
        if not octave_ok:
            wrong += 1
        else:
            errors.extend(errs)
            worst = max(worst, max(abs(e) for e in errs))
        if not quiet or not octave_ok:
            print(f"{r['file']:<44} {f0:9.3f}  " + "  ".join(f"{e:+8.4f}" for e in errs)
                  + "  harmonicity " + " ".join(f"{e.harmonicity:.2f}" for e in ests) + ("" if octave_ok else "  WRONG"))
    e = np.array(errors)
    print(f"vectors: {len(rows) - wrong} of {len(rows)} in the right octave at every position; "
          f"cents error sd {e.std():.4f}, |max| {worst:.4f}")
    return wrong + (1 if worst > 0.01 else 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--vectors", help="directory of WAV files with a manifest.csv")
    ap.add_argument("--quiet", action="store_true", help="print only failures, reports, and summaries")
    args = ap.parse_args()
    failures = run_synthetic(args.quiet)
    print(f"synthetic: {failures} failed")
    if args.vectors:
        failures += run_vectors(args.vectors, args.quiet)
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
