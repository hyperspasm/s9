#!/usr/bin/env python3
"""Emit test/golden/harmonic_series_golden.h: the fundamental, harmonicity, partial count
and uncertainty the harmonic series estimator gives for synthetic signals built from
partial tables, through the analyzer's spectrum: vowels with weak or missing
fundamentals, odd harmonics only, a sawtooth, a sine off its bin, noise, aliases, hum,
a sub-oscillator strong enough to be the fundamental and one too weak, an octave pair,
a fifth, and frames with no series at all.

    python validation/scripts/emit_harmonic_series_golden.py
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
import numpy as np  # noqa: E402

from s9 import harmonic_series, harmonics, recipes, signals, window  # noqa: E402
from _emit import c_array, c_double, c_string, header, repo_path  # noqa: E402

FS, N, AMP = 48000, 32768, 0.2
KMAX = 48     # harmonics kept in the tables, enough for every rule to act and small enough to embed


def cases():
    v = lambda f0: recipes.vowel(f0, FS, kmax=KMAX)   # noqa: E731
    return [
        # label, table, noise amplitude, seed
        ("vowel_25", v(25.0), 0.0, 1),
        ("vowel_110", v(110.0), 0.0, 1),
        ("vowel_440", v(440.0), 0.0, 1),
        ("vowel_3500", v(3500.0), 0.0, 1),
        ("vowel_h1_absent_220", recipes.without(v(220.0), 220.0, (1,)), 0.0, 1),
        ("vowel_h1_h2_absent_110", recipes.without(v(110.0), 110.0, (1, 2)), 0.0, 1),
        ("vowel_odd_only_55", recipes.odd_only(v(55.0), 55.0), 0.0, 1),
        ("sawtooth_880", recipes.sawtooth(880.0, FS, kmax=KMAX), 0.0, 1),
        ("sine_1000_7", recipes.sine(1000.7), 0.0, 1),
        ("vowel_noise_220", v(220.0), AMP * 10.0 ** (-20.0 / 20.0) * np.sqrt(3.0), 5),
        ("vowel_aliases_440", recipes.with_aliases(v(440.0), 440.0, FS, 44100.0, -25.0), 0.0, 1),
        ("vowel_hum_110", recipes.with_hum(v(110.0), 60.0, -40.0), 0.0, 1),
        ("vowel_sub_strong_220", recipes.sub_oscillator(v(220.0), 220.0, -6.0, FS), 0.0, 1),
        ("vowel_sub_weak_220", recipes.sub_oscillator(v(220.0), 220.0, -30.0, FS), 0.0, 1),
        ("vowel_octave_pair_440", recipes.detuned(v(440.0), 1200.0, FS, -3.0), 0.0, 1),
        ("vowel_fifth_220", recipes.detuned(v(220.0), 701.955, FS, 0.0), 0.0, 1),
        ("sine_6000_out_of_range", recipes.sine(6000.0), 0.0, 1),
        ("noise", [], AMP, 9),
        ("silence", [], 0.0, 1),
    ]


def main():
    w = window.create(window.BLACKMAN_HARRIS, N)
    reach, table = harmonics.lobe_reach(w), harmonics.correction_table(w)
    cfg = harmonic_series.Config()
    parts = []
    parts.append("#define HARMONIC_SERIES_GOLDEN_FS %d" % FS)
    parts.append("#define HARMONIC_SERIES_GOLDEN_N %d" % N)
    parts.append("#define HARMONIC_SERIES_GOLDEN_AMP %s" % c_double(AMP))
    parts.append("/* The configuration the estimates were made with */")
    parts.append("typedef struct { double f_min, f_max, peak_floor_db, prominence_db; unsigned shoulder; double strong_db; unsigned divisors;"
                 " double coarse_bins, fine_bins, sparse, completeness; unsigned min_extra; double extra_power, fit_floor_db, stray_bins; }"
                 " harmonic_series_golden_config_t;")
    parts.append("static const harmonic_series_golden_config_t harmonic_series_golden_config = { %s, %s, %s, %s, %u, %s, %u, %s, %s, %s, %s, %u, %s, %s, %s };"
                 % (c_double(cfg.f_min), c_double(cfg.f_max), c_double(cfg.peak_floor_db), c_double(cfg.prominence_db), cfg.shoulder,
                    c_double(cfg.strong_db), cfg.divisors, c_double(cfg.coarse_bins), c_double(cfg.fine_bins), c_double(cfg.sparse),
                    c_double(cfg.completeness), cfg.min_extra, c_double(cfg.extra_power), c_double(cfg.fit_floor_db), c_double(cfg.stray_bins)))
    parts.append("/* A case: partials as (frequency, amplitude, phase) triples summed at the amplitude, plus noise of noise_amp from the seed;"
                 " f0 is 0 where there is no series in range */")
    parts.append("typedef struct { const char *label; const double *partials; unsigned partial_count; double noise_amp; unsigned seed;"
                 " double f0, harmonicity; unsigned count; double uncertainty_cents; } harmonic_series_golden_case_t;")
    rows = []
    for label, partial_table, noise_amp, seed in cases():
        x = signals.generate(signals.PARTIALS, N, FS, amp=AMP, table=partial_table, seed=seed, noise_amp=noise_amp)
        db = harmonics.magnitude_db(harmonics.spectrum((x * w).astype(np.float32)), N)
        est = harmonic_series.estimate(db, FS / N, reach, table, cfg)
        flat = [v for triple in partial_table for v in triple]
        if flat:
            parts.append(c_array("harmonic_series_golden_%s_partials" % label, flat, per_line=3))
            ref = "harmonic_series_golden_%s_partials" % label
        else:
            ref = "NULL"
        rows.append("\t{ %s, %s, %u, %s, %u, %s, %s, %u, %s },"
                    % (c_string(label), ref, len(partial_table), c_double(noise_amp), seed, c_double(est.f0),
                       c_double(est.harmonicity), est.count, c_double(est.uncertainty_cents)))
        print(f"  {label:<26} f0 {est.f0:10.4f}  harmonicity {est.harmonicity:.4f}  count {est.count:4d}  ±{est.uncertainty_cents:.5f} cents",
              file=sys.stderr)
    parts.append("static const harmonic_series_golden_case_t harmonic_series_golden_cases[] = {\n" + "\n".join(rows) + "\n};")
    parts.append("#define HARMONIC_SERIES_GOLDEN_CASE_COUNT %d" % len(rows))

    out = repo_path("test", "golden", "harmonic_series_golden.h")
    with open(out, "w") as f:
        f.write(header("S9_TEST_HARMONIC_SERIES_GOLDEN_H", "emit_harmonic_series_golden.py",
                       "The harmonic series estimator's outputs for synthetic signals built from partial tables.", "\n".join(parts)))
    print(f"wrote {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
