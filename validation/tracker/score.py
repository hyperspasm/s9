"""Scoring a tracker's trajectory against a vector's truth, with the metrics of
objective.md, and aggregating the scores along the corpus's axes. A trajectory is
what any estimator returns: frames at some hop with, for each, the lock frequency
and its confidence, the fundamental and whether it is committed. An estimator that
has no lock stage reports its fundamental as the lock too.
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np

HOLD_S = 0.15            # draft: a glitch shorter than this is coasted through
LOCK_LATENCY_S = 0.15    # draft: a note is committed within this of its onset
FINE_CENTS = 50.0        # frames within this of the truth are counted as right
LOCK_CENTS = 5.0         # a lock latency ends at the first committed frame within this


@dataclass
class Trajectory:
    t: np.ndarray            # seconds: the instant each frame's values describe, a frame's centre
    lock_hz: np.ndarray      # 0 while unlocked
    lock_conf: np.ndarray
    f0_hz: np.ndarray        # the fundamental, 0 while undecided or unlocked
    committed: np.ndarray    # bool
    latency_s: float = 0.0   # how long after the instant it describes a frame's value can first be shown


def cents(a, b):
    with np.errstate(divide="ignore", invalid="ignore"):
        return 1200.0 * np.log2(a / b)


def _expected(truth, t):
    """the truth resampled at the frames: f0, voiced, tolerance, and whether validity is expected
    (voiced, past the lock latency of the last onset, and not in a glitch longer than the hold)"""
    f0 = np.interp(t, truth.t, truth.f0)
    voiced = np.interp(t, truth.t, truth.voiced.astype(float)) > 0.5
    tol = np.interp(t, truth.t, truth.tolerance_cents)
    expected = voiced.copy()
    for onset in truth.onsets:
        expected &= ~((t >= onset) & (t < onset + LOCK_LATENCY_S))
    for start, length in truth.glitches:
        if length > HOLD_S:
            expected &= ~((t >= start) & (t < start + length + LOCK_LATENCY_S))
    return f0, voiced, tol, expected


def score(truth, traj, tags=None):
    """the metrics for one vector, as a dict; NaN where a metric does not apply"""
    t = traj.t
    f0, voiced, tol, expected = _expected(truth, t)
    valid = traj.committed & (traj.f0_hz > 0)
    m = {}

    # voicing: precision over frames reported valid, recall over frames where validity is expected
    m["voicing_precision"] = float(np.mean(voiced[valid])) if np.any(valid) else (1.0 if not np.any(voiced) else np.nan)
    m["voicing_recall"] = float(np.mean(valid[expected])) if np.any(expected) else np.nan

    # pitch accuracy over frames where validity is expected: a miss counts as an error
    on = expected & valid & (f0 > 0)
    err = np.full(t.shape, np.nan)
    err[on] = cents(traj.f0_hz[on], f0[on])
    within = np.abs(err) <= np.maximum(FINE_CENTS, tol)
    m["raw_pitch_accuracy"] = float(np.sum(within & expected) / np.sum(expected)) if np.any(expected) else np.nan
    gross = on & ~within
    m["gross_error_rate"] = float(np.sum(gross) / np.sum(expected)) if np.any(expected) else np.nan
    octave = gross & (np.abs(np.abs(err) - 1200.0) <= FINE_CENTS)
    m["octave_error_rate"] = float(np.sum(octave) / np.sum(expected)) if np.any(expected) else np.nan
    fine = err[within & on]
    m["fine_error_median"] = float(np.median(np.abs(fine))) if fine.size else np.nan
    m["fine_error_p90"] = float(np.percentile(np.abs(fine), 90)) if fine.size else np.nan
    m["bias"] = float(np.mean(fine)) if fine.size else np.nan
    m["jitter"] = float(np.std(fine)) if fine.size > 1 else np.nan

    # lock latency: from each onset to when the first committed frame within LOCK_CENTS can be shown
    latencies = []
    for onset in truth.onsets:
        after = (t >= onset) & voiced & valid & (f0 > 0)
        good = after & (np.abs(cents(np.where(traj.f0_hz > 0, traj.f0_hz, 1.0), np.where(f0 > 0, f0, 1.0))) <= LOCK_CENTS)
        latencies.append(float(t[good][0] - onset + traj.latency_s) if np.any(good) else np.nan)
    m["lock_latency"] = float(np.nanmax(latencies)) if latencies and not np.all(np.isnan(latencies)) else np.nan
    m["lock_failures"] = int(np.sum(np.isnan(latencies))) if latencies else 0
    m["latency"] = float(traj.latency_s)

    # revisions: octave jumps of the committed fundamental within a note
    rev = 0
    prev = 0.0
    for i in range(t.shape[0]):
        if valid[i] and expected[i]:
            if prev > 0.0 and abs(cents(traj.f0_hz[i], prev)) > 600.0:
                rev += 1
            prev = traj.f0_hz[i]
        elif not voiced[i]:
            prev = 0.0
    m["revisions"] = rev

    # the lock stage: how much of the voiced note it holds, and whether it stays on one harmonic of the truth
    locked = traj.lock_hz > 0
    m["lock_coverage"] = float(np.mean(locked[expected])) if np.any(expected) else np.nan
    ratio = np.full(t.shape, np.nan)
    sel = locked & (f0 > 0) & voiced
    ratio[sel] = traj.lock_hz[sel] / f0[sel]
    if np.any(sel):
        k = np.round(ratio[sel] * 4.0) / 4.0   # to a quarter, so k = 1, 2, 3, 1/2 ...
        m["lock_ratio_changes"] = int(np.sum(np.diff(k) != 0))
        m["lock_on_series"] = float(np.mean(np.abs(cents(ratio[sel], k)) <= FINE_CENTS))
    else:
        m["lock_ratio_changes"], m["lock_on_series"] = np.nan, np.nan

    if tags:
        m.update({f"tag:{k}": v for k, v in tags.items()})
    return m


def modulation_response(truth, traj, rate_hz):
    """gain and lag of the tracked vibrato against the true one at the known rate: a
    sinusoid at rate_hz fitted by least squares to each trajectory's cents about its mean.
    The lag is the estimator's own, in cycles; what a player sees lags by its latency too."""
    t = traj.t
    f0 = np.interp(t, truth.t, truth.f0)
    sel = (traj.f0_hz > 0) & (f0 > 0) & (t > 0.5)
    if np.sum(sel) < 10:
        return np.nan, np.nan
    ref = f0[sel]
    base = np.exp(np.mean(np.log(ref)))
    y_true = cents(ref, base)
    y_est = cents(traj.f0_hz[sel], base)
    w = 2.0 * np.pi * rate_hz * t[sel]
    basis = np.stack([np.cos(w), np.sin(w), np.ones_like(w)], axis=1)
    a_true, _, _, _ = np.linalg.lstsq(basis, y_true, rcond=None)
    a_est, _, _, _ = np.linalg.lstsq(basis, y_est, rcond=None)
    amp_true, amp_est = np.hypot(a_true[0], a_true[1]), np.hypot(a_est[0], a_est[1])
    # y = a cos + b sin = A cos(wt - phi) with phi = atan2(b, a); a delay of tau adds w tau to phi
    phase = np.arctan2(a_est[1], a_est[0]) - np.arctan2(a_true[1], a_true[0])
    phase = (phase + np.pi) % (2.0 * np.pi) - np.pi
    lag_cycles = phase / (2.0 * np.pi)
    return float(amp_est / amp_true) if amp_true > 0 else np.nan, float(lag_cycles)


def aggregate(rows, axis):
    """the mean of every metric over the rows sharing each value of an axis tag"""
    key = f"tag:{axis}"
    groups = {}
    for r in rows:
        if key in r:
            groups.setdefault(r[key], []).append(r)
    out = {}
    for value, members in groups.items():
        metrics = {}
        for name in members[0]:
            if name.startswith("tag:") or name == "id":
                continue
            vals = np.array([m[name] for m in members if isinstance(m.get(name), (int, float))], dtype=float)
            metrics[name] = float(np.nanmean(vals)) if vals.size and not np.all(np.isnan(vals)) else np.nan
        metrics["count"] = len(members)
        out[value] = metrics
    return out


REPORT_COLUMNS = ["raw_pitch_accuracy", "gross_error_rate", "octave_error_rate", "fine_error_median", "fine_error_p90",
                  "jitter", "voicing_precision", "voicing_recall", "lock_latency", "revisions", "lock_coverage", "lock_ratio_changes"]
COLUMN_HEADS = ["rpa", "gross", "octave", "fine_med", "fine_p90", "jitter", "voice_p", "voice_r", "lock_lat", "revis", "lock_cov", "ratio_chg"]


def table(groups, title):
    """a text table of the aggregated metrics, one row per axis value"""
    lines = [title, " " * 22 + " ".join(f"{h:>9s}" for h in COLUMN_HEADS) + "   n"]

    def order(kv):
        v = kv[0]
        return (0, float(v)) if isinstance(v, (int, float)) else (1, str(v))

    for value, m in sorted(groups.items(), key=order):
        cells = []
        for c in REPORT_COLUMNS:
            v = m.get(c, np.nan)
            cells.append(f"{v:9.3f}" if isinstance(v, float) and not np.isnan(v) else f"{'-':>9s}")
        lines.append(f"{str(value)[:22]:22s}" + " ".join(cells) + f"  {m['count']:3d}")
    return "\n".join(lines)
