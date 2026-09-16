"""Partial tables for test signals: a series of (frequency, amplitude, phase) triples
that signals.partials() sums. The recipes make the shapes that trouble a fundamental
estimator: a vowel with a weak fundamental and formants, the same with harmonics
missing or only the odd ones, a sub-oscillator's real subharmonic series, aliases folded
about another rate, mains hum, a stretched (inharmonic) string, a detuned unison. Each
table is scaled to unit RMS, so a signal's amplitude is its RMS.
"""
import math


def _lcg_phases(count, seed):
    """count phases in [0, 2 pi) from a linear congruential generator."""
    out, s = [], seed & 0xFFFFFFFF
    for _ in range(count):
        s = (1664525 * s + 1013904223) & 0xFFFFFFFF
        out.append(2.0 * math.pi * (s >> 8) / 16777216.0)
    return out


def normalized(table):
    """The table scaled to unit RMS: the sum of a^2 / 2 is 1."""
    power = sum(a * a / 2.0 for _, a, _ in table)
    scale = 1.0 / math.sqrt(power) if power > 0.0 else 1.0
    return [(f, a * scale, p) for f, a, p in table]


def strongest(table):
    return max(a for _, a, _ in table) if table else 0.0


def formant_lift(f, f1, f2, q1, q2):
    """Two resonances, the second half as strong, over a floor between them."""
    return 1.0 / math.sqrt(1.0 + ((f - f1) / q1) ** 2) + 0.5 / math.sqrt(1.0 + ((f - f2) / q2) ** 2) + 0.02


def vowel(f0, fs, f1=700.0, f2=1800.0, q1=120.0, q2=200.0, h1_db=-30.0, slope_db=-12.0, kmax=None, seed=1):
    """A glottal-like harmonic series shaped by two formants: harmonics of f0 up to
    just below half the sample rate (or kmax), the source falling slope_db per octave,
    the formants at f1 and f2 with half-widths q1 and q2 lifting them, and the
    fundamental h1_db below where the source alone would put it."""
    table = []
    k = 1
    while k * f0 < fs / 2.0 - 50.0 and (kmax is None or k <= kmax):
        f = k * f0
        a = (1.0 / k) ** (-slope_db / 6.0206) * formant_lift(f, f1, f2, q1, q2)
        if k == 1:
            a *= 10.0 ** (h1_db / 20.0)
        table.append((f, a, 0.0))
        k += 1
    phases = _lcg_phases(len(table), seed)
    return normalized([(f, a, p) for (f, a, _), p in zip(table, phases)])


def without(table, f0, ks):
    """The table without harmonics ks of f0."""
    return normalized([(f, a, p) for f, a, p in table if round(f / f0) not in ks])


def odd_only(table, f0):
    return normalized([(f, a, p) for f, a, p in table if round(f / f0) % 2 == 1])


def sub_oscillator(table, f0, level_db, fs, seed=2):
    """A square sub-oscillator an octave below: odd harmonics of f0 / 2, the first
    level_db relative to the table's strongest partial, falling as 1 / k."""
    top = strongest(table)
    extra = []
    j = 1
    while j * f0 / 2.0 < fs / 2.0 - 50.0:
        extra.append((j * f0 / 2.0, top * 10.0 ** (level_db / 20.0) / j, 0.0))
        j += 2
    phases = _lcg_phases(len(extra), seed)
    return normalized(table + [(f, a, p) for (f, a, _), p in zip(extra, phases)])


def with_aliases(table, f0, fs, rate, level_db, count=20, seed=3):
    """Partials folded about another sample rate, as a synthesizer that ran at `rate`
    without band-limiting leaves them: harmonics of f0 above rate / 2 land at
    |rate - k f0|, count of them, at level_db relative to the strongest partial."""
    top = strongest(table)
    extra = []
    k = int(rate / 2.0 / f0) + 1
    while len(extra) < count:
        fa = abs(rate - k * f0)
        if 30.0 < fa < fs / 2.0 - 50.0:
            extra.append((fa, top * 10.0 ** (level_db / 20.0), 0.0))
        k += 1
        if k * f0 > 4.0 * rate:
            break
    phases = _lcg_phases(len(extra), seed)
    return normalized(table + [(f, a, p) for (f, a, _), p in zip(extra, phases)])


def with_hum(table, mains, level_db, count=8, seed=4):
    """Mains hum: harmonics of mains at level_db relative to the strongest partial,
    falling 6 dB per harmonic."""
    top = strongest(table)
    extra = [(mains * k, top * 10.0 ** (level_db / 20.0) / k, 0.0) for k in range(1, count + 1)]
    phases = _lcg_phases(len(extra), seed)
    return normalized(table + [(f, a, p) for (f, a, _), p in zip(extra, phases)])


def stretched(f0, fs, inharmonicity, kmax=30, seed=5):
    """A string's partials, k f0 sqrt(1 + B k^2), falling as 1 / k."""
    table = []
    for k in range(1, kmax + 1):
        f = k * f0 * math.sqrt(1.0 + inharmonicity * k * k)
        if f > fs / 2.0 - 50.0:
            break
        table.append((f, 1.0 / k, 0.0))
    phases = _lcg_phases(len(table), seed)
    return normalized([(f, a, p) for (f, a, _), p in zip(table, phases)])


def detuned(table, cents, fs, level_db=0.0, seed=6):
    """The table plus a copy of itself shifted by cents, at level_db, without what the
    shift carries above half the sample rate."""
    ratio = 2.0 ** (cents / 1200.0)
    gain = 10.0 ** (level_db / 20.0)
    copy = [(f * ratio, a * gain, 0.0) for f, a, _ in table if f * ratio < fs / 2.0 - 50.0]
    phases = _lcg_phases(len(copy), seed)
    return normalized(table + [(f, a, p) for (f, a, _), p in zip(copy, phases)])


def sawtooth(f0, fs, kmax=None, seed=7):
    """Every harmonic, falling as 1 / k."""
    table = []
    k = 1
    while k * f0 < fs / 2.0 - 50.0 and (kmax is None or k <= kmax):
        table.append((k * f0, 1.0 / k, 0.0))
        k += 1
    phases = _lcg_phases(len(table), seed)
    return normalized([(f, a, p) for (f, a, _), p in zip(table, phases)])


def sine(f):
    return [(f, math.sqrt(2.0), 0.0)]
