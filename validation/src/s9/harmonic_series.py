"""The fundamental of the harmonic series in a spectrum.

Partials are the prominent peaks of the magnitude spectrum in dB: local maxima within
peak_floor_db of the strongest that stand prominence_db above the floor beside them,
the lower of two shoulder medians past the window's lobe, each refined by a parabola
less the window's bias. The fundamental is found in two stages. First, the largest
frequency of which every strong partial, within strong_db of the strongest, is a
multiple, taking the lowest strong partial as harmonic 1, 2, 3, ... until the rest
fall on the series; a series above f_max means no fundamental in range, one a little
below f_min still counts, and when no series holds every strong partial, the one holding most of their power is taken among
those whose partials fill at least a fraction of the slots between the lowest and the
highest of them. Second, a sub-multiple of that is taken instead only when the weaker partials fill more
than a fraction of the extra slots it predicts between the lowest and highest partial on
it, with at least min_extra of them carrying more than extra_power of the series' power:
what a real subharmonic series does and aliases or noise do not. The fine value is a
least squares of f_k = k f0 over the partials on the series, weighted by their power and
by how closely they lie on it, so a stray peak near a multiple barely counts. With it
come the harmonicity, the frame's power within the partials on the series over its
total, the count of those partials, and an uncertainty in cents from their spread. The
fit leaves out partials more than fit_floor_db below the strongest: they add nothing to
its precision, and in noise they are as likely stray peaks as harmonics.
"""
import math

import numpy as np

from . import harmonics

F_MIN_MARGIN = 0.02   # a series this fraction below f_min still counts: a fundamental at the edge is not a coin toss


class Config:
    def __init__(self, f_min=20.0, f_max=4000.0, peak_floor_db=-60.0, prominence_db=12.0, shoulder=8,
                 strong_db=20.0, divisors=32, coarse_bins=1.5, fine_bins=0.5, sparse=0.3, completeness=0.5,
                 min_extra=2, extra_power=0.05, fit_floor_db=40.0, stray_bins=0.05):
        self.f_min = f_min                  # Hz, the fundamentals looked for
        self.f_max = f_max
        self.peak_floor_db = peak_floor_db  # peaks this far below the strongest are not partials
        self.prominence_db = prominence_db  # a partial stands this far above the floor beside it
        self.shoulder = shoulder            # bins of spectrum on each side, past the lobe, that make the floor
        self.strong_db = strong_db          # partials within this of the strongest anchor the series
        self.divisors = divisors            # the lowest strong partial is tried as this many harmonics
        self.coarse_bins = coarse_bins      # a partial this close to a multiple of a first candidate is on it
        self.fine_bins = fine_bins          # and of a refined one
        self.sparse = sparse                # a series taken for holding the most power must fill this fraction of its slots
        self.completeness = completeness    # a sub-multiple must fill this fraction of its extra slots
        self.min_extra = min_extra          # with at least this many partials
        self.extra_power = extra_power      # carrying this fraction of the series' power
        self.fit_floor_db = fit_floor_db    # partials this far below the strongest stay out of the fit
        self.stray_bins = stray_bins        # a partial this far off its multiple weighs half in the fit


class Estimate:
    def __init__(self, f0=0.0, harmonicity=0.0, count=0, uncertainty_cents=0.0):
        self.f0 = f0                        # Hz; 0 when there is no series in range
        self.harmonicity = harmonicity      # the frame's power within the partials on the series, over its total
        self.count = count                  # partials on the series
        self.uncertainty_cents = uncertainty_cents


def partials(db, delta_f, reach, table, cfg):
    """The prominent peaks: parallel arrays of frequency (Hz) and level (dB)."""
    n = len(db)
    top = -math.inf
    for p in range(1, n - 1):
        if db[p] > top and db[p] > db[p - 1] and db[p] >= db[p + 1]:
            top = db[p]
    freqs, levels = [], []
    for p in range(1, n - 1):
        if not (db[p] > db[p - 1] and db[p] >= db[p + 1] and db[p] > top + cfg.peak_floor_db):
            continue
        floor = harmonics.floor_db(db, p, reach, cfg.shoulder)
        if not db[p] > floor + cfg.prominence_db:
            continue
        d, peak = harmonics.parabola(float(db[p - 1]), float(db[p]), float(db[p + 1]))
        off, level = harmonics.corrected(d, peak, table)
        freqs.append((p + off) * delta_f)
        levels.append(level)
    return np.array(freqs), np.array(levels)


def on_series(c, freqs, delta_f, tol_bins):
    """Which partials lie within tol_bins of a multiple of c, and their multiples."""
    k = np.rint(freqs / c)
    return (k >= 1) & (np.abs(freqs - k * c) < tol_bins * delta_f), k


def filled(m, k):
    """The fraction of the slots between the lowest and the highest multiple matched that
    are matched."""
    ks = np.unique(k[m].astype(int))
    return len(ks) / (int(ks.max()) - int(ks.min()) + 1) if len(ks) else 0.0


def fit(c, freqs, pw, delta_f, tol_bins, usable):
    """The least squares of f_k = k c over the usable partials on the series of c, weighted
    by power: c, the partials on the series, their multiples."""
    m, k = on_series(c, freqs, delta_f, tol_bins)
    u = m & usable
    if not u.any():
        return c, m, k
    w = pw[u]
    return float(np.sum(w * k[u] * freqs[u]) / np.sum(w * k[u] ** 2)), m, k


def estimate(db, delta_f, reach, table, cfg):
    """The fundamental of the series in a magnitude spectrum in dB (bins 0..n-1 at
    delta_f apart), with the window's lobe reach and bias table."""
    freqs, levels = partials(db, delta_f, reach, table, cfg)
    keep = (freqs >= cfg.f_min * 0.9) & (freqs <= (len(db) - 3) * delta_f)
    freqs, levels = freqs[keep], levels[keep]
    if len(freqs) == 0:
        return Estimate()
    pw = 10.0 ** (levels / 10.0)
    strong = levels > levels.max() - cfg.strong_db
    usable = levels > levels.max() - cfg.fit_floor_db

    # stage one: the largest frequency of which every strong partial is a multiple
    f_low = float(freqs[strong].min())
    lowest = cfg.f_min * (1.0 - F_MIN_MARGIN)
    c0 = None
    for k in range(1, cfg.divisors + 1):
        c = f_low / k
        if c < lowest:
            break
        m, _ = on_series(c, freqs, delta_f, cfg.coarse_bins)
        if np.all(m[strong]):
            if c > cfg.f_max:
                return Estimate()
            c0 = c
            break
    if c0 is None:
        held_best = -1.0
        for k in range(1, cfg.divisors + 1):
            c = f_low / k
            if c < lowest:
                break
            if c > cfg.f_max:
                continue
            m, kk = on_series(c, freqs, delta_f, cfg.coarse_bins)
            if filled(m, kk) < cfg.sparse:
                continue
            held = float(pw[m & strong].sum())
            if held > held_best:
                held_best, c0 = held, c
        if c0 is None:
            return Estimate()
    c0, _, _ = fit(c0, freqs, pw, delta_f, cfg.coarse_bins, usable)
    c0, _, _ = fit(c0, freqs, pw, delta_f, cfg.fine_bins, usable)

    # stage two: the deepest sub-multiple the weaker partials support
    chosen = c0
    for divisor in range(2, cfg.divisors + 1):
        c = c0 / divisor
        if c < lowest:
            break
        m, k = on_series(c, freqs, delta_f, cfg.fine_bins)
        if not m.any():
            continue
        ks = np.unique(k[m].astype(int))
        extra = ks[ks % divisor != 0]
        slots = sum(1 for j in range(int(ks.min()), int(ks.max()) + 1) if j % divisor != 0)
        extra_power = float(pw[m & (k.astype(int) % divisor != 0)].sum() / pw[m].sum())
        if len(extra) >= cfg.min_extra and slots > 0 and len(extra) / slots > cfg.completeness and extra_power > cfg.extra_power:
            chosen = c

    # the fine value: weighted by power, then also by closeness to the series
    f0, m, k = fit(chosen, freqs, pw, delta_f, cfg.fine_bins, usable)
    u = m & usable
    w = pw[u].copy()
    for _ in range(3):
        f0 = float(np.sum(w * k[u] * freqs[u]) / np.sum(w * k[u] ** 2))
        dev = (freqs[u] - k[u] * f0) / delta_f
        w = pw[u] / (1.0 + (dev / cfg.stray_bins) ** 2)

    # harmonicity, count, and the uncertainty of the fit
    total = float(np.sum(10.0 ** (db / 10.0)))
    held = 0.0
    for b in np.rint(freqs[m] / delta_f).astype(int):
        lo, hi = max(0, b - reach), min(len(db), b + reach + 1)
        held += float(np.sum(10.0 ** (db[lo:hi] / 10.0)))
    harmonicity = min(held / total, 1.0) if total > 0.0 else 0.0
    wk = w * k[u] ** 2                                  # the weights of the mean of f_k / k
    r = freqs[u] / k[u] - f0
    se = math.sqrt(float(np.sum(wk ** 2 * r ** 2))) / float(np.sum(wk)) if np.sum(wk) > 0 else 0.0
    return Estimate(f0, harmonicity, int(m.sum()), 1200.0 / math.log(2.0) * se / f0)
