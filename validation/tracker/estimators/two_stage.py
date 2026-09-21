"""The two-stage tracker of objective.md, prototyped: a lock that follows a
periodicity, and a fundamental decided from the spectrum as a small rational of
the lock.

The lock (Lock) is a local period search with memory. Unlocked, it looks for a
period over the whole range with YIN's normalized difference function and locks
when consecutive frames agree. Locked, each hop it correlates a window of a few
periods against itself over lags within a fraction of the current period, takes
the best lag refined by a parabola, and follows it; the window shortens while the
pitch moves quickly, so the correlation stays sharp; a hop with no usable peak is
coasted through for up to a hold time, during which the global search runs near the
lost period, resuming the lock if it finds the same note and taking a new one once
frames agree on it; past the hold the lock is lost. The lock's frequency is what
moves; it is stamped at the middle of the sample pairs its correlation compared, and
reported through a smoother whose time constant is long while the lock is still and
short while it moves. When the fundamental stage finds the series at a multiple of
the lock, the lock retunes down to it.

The fundamental (Fundamental) runs every hop over a longer frame sized to resolve
partials at the lock's frequency. The frame is first warped by the lock's own
history so that the pitch is constant across it, which keeps the partials sharp
through vibrato and glides. Its prominent spectral peaks are read, each refined by a
parabola through its neighbours less the window's bias in it, and those that are
both prominent and within the balance the objective promises count as evidence.
A coarse comb of the lock's harmonics is refined by the lowest partials on it, and
then the comb and its octave sub-multiples are judged: the lock's comb is the
fundamental's unless a sub-multiple's own slots hold evidence, below twice the lock,
that persists; the greatest common divisor of the evident harmonic numbers lifts a
lock that sits on a sub-multiple, once that too has persisted and the waveform is
found to repeat at the shorter period nearly as well as at the lock's; and a comb whose
harmonics split into multiples of two coprime numbers is two voices, of which the
louder is taken. A decision commits once the comb holds most of the evident power
for a few evaluations, and is revised when another has held for longer, contrary
evaluations counting down rather than resetting; while the lock holds, a committed
decision survives evidence going quiet, and lapses only after a while. A weighted
least squares over the peaks on the chosen comb refines the value: the reported
fundamental is the lock over its ratio, corrected slowly toward that fit, so it
moves with the lock and settles to the fit's precision.
"""
from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np

from s9 import harmonics as oracle   # the readout's model of a window's peak, for refining peaks
from ..score import Trajectory


@dataclass
class Config:
    hop_s: float = 0.01
    f_min: float = 30.0                # C1 and C7 with room for a deep vibrato
    f_max: float = 2400.0
    gate_dbfs: float = -75.0           # frames quieter than this are silence: well under the quietest note promised
    # the lock
    acquire_frames_s: tuple = (0.032, 0.096)   # the frames the global search runs on, short first: each finds
                                               # periods up to a third of itself, so the short one serves most of
                                               # the range quickly and the long one the bottom
    acquire_threshold: float = 0.2      # YIN's threshold on the normalized difference
    acquire_agree: int = 2              # consecutive frames agreeing within acquire_tolerance before locking
    acquire_tolerance: float = 0.06     # as a fraction of the period, frame to frame
    periods_in_window: float = 6.0      # the tracking window, in periods, within the bounds
    window_min_s: float = 0.025
    window_max_s: float = 0.12
    window_drift: float = 0.03          # the window is shortened so the pitch moves less than this across it
    search: float = 0.12                # lags searched, as a fraction of the period either way
    track_min_corr: float = 0.5         # a correlation peak below this is a miss
    hold_s: float = 0.15                # misses coasted through before the lock is lost
    smooth_max_s: float = 0.15          # the lock's frequency is reported smoothed over up to this long while it
                                        # is still, and over less while it moves...
    smooth_drift_cents: float = 3.0     # ...so that the smoothing lags the motion by no more than about this
    # the fundamental
    cycles_in_frame: float = 16.0       # the spectral frame, in cycles of the lock, within the bounds: enough
                                        # that a partial's shoulders lie clear of its neighbours
    frame_min: int = 4096
    frame_max: int = 16384
    prominence_db: float = 12.0         # a peak stands this far above the floor beside it
    shoulder: int = 12                  # bins of floor read on each side of a peak, past the window's lobe
    lobe_reach: int = 4                 # bins a Blackman-Harris peak raises on each side
    peak_floor_db: float = 60.0         # peaks this far below the strongest are ignored
    coarse_fraction: float = 0.03       # a peak this near a harmonic of the lock, as a fraction, is on the coarse comb
    refine_harmonics: int = 4           # the lowest harmonics on it refine the comb's fundamental
    on_comb_bins: float = 0.15          # a peak this near a harmonic, in bins, plus a fraction of it...
    on_comb_fraction: float = 0.004     # ...plus the stretch a string would give it, is on the refined comb
    inharmonicity_max: float = 4e-4     # the stretch allowed: a string's B at middle C, growing in proportion to
                                        # the pitch, at twice a piano's
    slot_fraction: float = 0.25         # but a slot never reaches past this fraction of the way to its neighbours
    comb_span: float = 12.0             # peaks up to this many times the lock's frequency take part
    balance_db: float = 40.0            # evidence of the series is a peak within this of the strongest peak...
    evidence_prominence_db: float = 15.0   # ...standing this far above the floor beside it
    sub_multiples: tuple = (2, 4)       # the octaves: a series at any other ratio below the lock is another source
    evidence_span: float = 2.0          # a sub-multiple's evidence lies below this many times the lock's frequency,
                                        # in its lowest own slots, where a series has its energy
    persistence: float = 0.6            # a sub-multiple's evidence, or a lift's, counts once present in this fraction
                                        # of recent evaluations, leakily counted...
    persistence_leak: float = 0.15      # ...with this step per evaluation
    pending: float = 0.3                # while such evidence has built past this, the lock's own comb waits
    lift_slack: float = 0.1             # a lift's shorter period may repeat this much less well than the lock's own
    voices: tuple = (2, 3, 4, 5, 6)     # multiples tried as two voices sharing a comb
    harmonicity_on: float = 0.6         # the comb's share of the evident peaks' power, to commit: two unrelated
                                        # tones share it equally and must not
    harmonicity_off: float = 0.3        # and to stay committed
    lapse_s: float = 1.0                # a committed decision lapses after this long below harmonicity_off
    commit_evals: int = 3               # evaluations a decision must hold before it commits
    revise_evals: int = 40              # and a different one before it replaces a committed decision
    correction_tc_s: float = 0.5        # the lock's correction toward the fit follows with this time constant
    correction_max: float = 0.02        # and never exceeds this fraction: a stretched string's period sits well above its lowest partial
    seed_peaks: int = 4                 # a decision starts from the fit's correction when the fit rests on this many peaks


MIDDLE_C_HZ = 261.63


def _blackman_harris(n):
    k = np.arange(n)
    return 0.35875 - 0.48829 * np.cos(2 * np.pi * k / n) + 0.14128 * np.cos(4 * np.pi * k / n) - 0.01168 * np.cos(6 * np.pi * k / n)


def _parabola(a, b, c):
    denom = a - 2.0 * b + c
    shift = 0.5 * (a - c) / denom if abs(denom) > 1e-12 else 0.0
    return shift, b - 0.25 * (a - c) * shift


class Lock:
    def __init__(self, cfg, fs):
        self.cfg, self.fs = cfg, fs
        self.hop = int(cfg.hop_s * fs)
        self.period = 0.0        # samples; 0 while unlocked
        self.corr = 0.0
        self.coast_s = 0.0
        self.rate = 0.0          # fractional change of the period per hop, smoothed, signed...
        self.slew = 0.0          # ...and in magnitude
        self.centre = 0.0        # samples before the hop's end that the period describes: the middle of
                                 # the pairs of samples compared, which trail the window by half a lag
        self.candidates = []     # periods from the global search, for agreement
        self.tau_min = int(fs / cfg.f_max)
        self.tau_max = int(fs / cfg.f_min)
        self.last_period = 0.0   # the period the lock last had, and how long ago it was lost or slipped
        self.lost_s = 0.0
        self.relocked = False    # this hop's period came from a search, not from tracking
        self.smooth = 0.0        # the reported frequency, log2, smoothed by how still the lock is
        self.lag = 0.0           # how far the smoothed frequency trails the measured one, cents, smoothed

    @property
    def locked(self):
        return self.period > 0.0

    @property
    def frequency(self):
        return self.fs / self.period if self.period > 0.0 else 0.0

    @property
    def reported(self):
        return 2.0 ** self.smooth if self.period > 0.0 else 0.0

    def _smooth(self):
        """follow the frequency with a time constant set by its motion: long while it is
        still, so its jitter averages out, short while it moves, so the report keeps up.
        The motion is the larger of the rate of change and how far the smoothed value has
        fallen behind, both smoothed so that jitter, which has no direction, counts little."""
        cfg = self.cfg
        f = math.log2(self.frequency)
        if self.relocked or self.smooth == 0.0:
            self.smooth, self.lag = f, 0.0
            return
        self.lag = 0.7 * self.lag + 0.3 * (f - self.smooth) * 1200.0
        motion = max(abs(self.rate) * 1200.0 / math.log(2.0), abs(self.lag))
        tc_hops = cfg.smooth_max_s / cfg.hop_s
        if motion > 0.0:
            tc_hops = min(tc_hops, cfg.smooth_drift_cents / motion)
        self.smooth += (f - self.smooth) / max(tc_hops, 1.0)

    def window_len(self):
        cfg = self.cfg
        n = cfg.periods_in_window * self.period
        if self.slew > 1e-6:
            n = min(n, cfg.window_drift / self.slew * self.hop)   # shorter while the pitch moves
        n = max(n, 2.0 * self.period)
        return int(min(max(n, cfg.window_min_s * self.fs), cfg.window_max_s * self.fs))

    # -- the global search, YIN's difference function over the whole range

    def _acquire(self, x, end):
        """the global search: a candidate period from each frame, short frames first; the
        lock is taken once consecutive candidates agree. Just after a lock was lost or
        slipped, the search stays near where it was: lags within a factor of three."""
        quick = self.last_period > 0.0 and self.lost_s <= self.cfg.hold_s * 2.0
        tau_lo = max(self.tau_min, int(self.last_period / 3.0)) if quick else self.tau_min
        tau_hi = min(self.tau_max, int(3.0 * self.last_period)) if quick else self.tau_max
        found = None
        for frame_s in self.cfg.acquire_frames_s:
            n = int(frame_s * self.fs)
            tau_max = min(tau_hi, n // 3)
            if tau_max <= tau_lo + 2 or end < n:
                continue
            found = self._search(x[end - n:end], tau_lo, tau_max)
            if found is not None:
                break
        if found is None:
            self.candidates = []
            return
        period, value, centre = found
        if self.locked and abs(period / self.period - 1.0) <= self.cfg.search:
            # the note coasted through is still there: the lock resumes rather than starting over
            self.period, self.corr, self.centre, self.coast_s = period, 1.0 - value, centre, 0.0
            self.candidates = []
            return
        self.candidates.append(period)
        recent = self.candidates[-self.cfg.acquire_agree:]
        # consecutive frames agreeing, each with the last, so a moving pitch can still lock
        if len(recent) >= self.cfg.acquire_agree and all(
                abs(recent[i] / recent[i - 1] - 1.0) <= self.cfg.acquire_tolerance for i in range(1, len(recent))):
            self.period = recent[-1]
            self.corr = 1.0 - value
            self.centre = centre
            self.coast_s = 0.0
            self.rate, self.slew = 0.0, 0.0
            self.candidates = []
            self.relocked = True

    def _search(self, frame, tau_min, tau_max):
        """YIN's first dip under the threshold over a frame: (period, depth, samples before
        the frame's end its period describes), or None"""
        n = frame.shape[0]
        w = max(n - tau_max, tau_max)
        size = 1 << (2 * n).bit_length()
        spec = np.fft.rfft(frame, size)
        spec_w = np.fft.rfft(frame[:w], size)
        acf = np.fft.irfft(np.conj(spec_w) * spec, size)[:tau_max + 1]   # sum x[i] x[i+tau] over i < w
        energy = np.concatenate([[0.0], np.cumsum(frame * frame)])
        e0 = energy[w]
        if e0 <= 1e-6 * energy[n]:
            return None   # the part compared is silent while the frame is not: an onset, no period yet
        e_tau = energy[w + np.arange(tau_max + 1)] - energy[np.arange(tau_max + 1)]
        d = e0 + e_tau - 2.0 * acf
        cmndf = np.ones_like(d)
        running = np.cumsum(d[1:])
        cmndf[1:] = d[1:] * np.arange(1, tau_max + 1) / np.where(running > 0.0, running, 1e-12)
        tau = max(tau_min, 1)
        found = None
        while tau < tau_max:
            if cmndf[tau] < self.cfg.acquire_threshold:
                while tau + 1 < tau_max and cmndf[tau + 1] < cmndf[tau]:
                    tau += 1
                found = tau
                break
            tau += 1
        if found is None or cmndf[found - 1] < cmndf[found]:
            return None   # no dip, or the flank of one at a shorter lag than the range
        shift, value = _parabola(cmndf[found - 1], cmndf[found], cmndf[found + 1])
        period = found + shift
        return period, value, n - (w + period) / 2.0

    # -- tracking: the best lag near the period, by normalized correlation

    def _correlate(self, x, end, n, lo, hi):
        """the best lag in lo..hi of the last n samples against themselves, by normalized
        correlation, refined by a parabola: (period, correlation), or None off a usable peak"""
        seg = x[end - n - hi:end]              # the window and enough before it for every lag
        ref = seg[hi:]                          # the last n samples
        lags = np.arange(lo, hi + 1)
        size = 1 << (n + hi).bit_length()
        spec_seg = np.fft.rfft(seg, size)
        spec_ref = np.fft.rfft(ref, size)
        corr_full = np.fft.irfft(spec_seg * np.conj(spec_ref), size)   # corr_full[m] = sum seg[i + m] ref[i]
        corr = corr_full[hi - lags]
        energy = np.concatenate([[0.0], np.cumsum(seg * seg)])
        e_ref = energy[hi + n] - energy[hi]
        e_lag = energy[hi - lags + n] - energy[hi - lags]
        r = corr / np.sqrt(np.maximum(e_ref * e_lag, 1e-30))
        i = int(np.argmax(r))
        if r[i] < self.cfg.track_min_corr or i == 0 or i == len(lags) - 1:
            return None
        shift, value = _parabola(r[i - 1], r[i], r[i + 1])
        return float(lags[i] + shift), float(value)

    def _track(self, x, end):
        cfg = self.cfg
        n = self.window_len()
        lo = max(self.tau_min, int(self.period * (1.0 - cfg.search)))
        hi = min(self.tau_max, int(math.ceil(self.period * (1.0 + cfg.search))))
        if end < n + hi or hi <= lo + 2:
            return self._miss()
        found = self._correlate(x, end, n, lo, hi)
        if found is None:
            return self._miss()
        period, value = found
        step = period / self.period - 1.0
        self.rate = 0.7 * self.rate + 0.3 * step
        self.slew = 0.7 * self.slew + 0.3 * abs(step)
        self.period = period
        self.corr = value
        self.centre = (n + period) / 2.0
        self.coast_s = 0.0

    def repeats_at(self, x, end, divisor):
        """whether the waveform repeats at a divisor of the lock's period nearly as well as
        at the period itself, and well enough to be tracked there"""
        n = self.window_len()
        lag = int(round(self.period / divisor))
        if lag < self.tau_min or end < n + lag:
            return False
        ref = x[end - n:end]
        lagged = x[end - n - lag:end - lag]
        r = float(np.dot(ref, lagged) / math.sqrt(max(np.dot(ref, ref) * np.dot(lagged, lagged), 1e-30)))
        return r >= max(self.cfg.track_min_corr, self.corr - self.cfg.lift_slack)

    def retune(self, x, end, divisor):
        """move the lock down to a divisor of its period, where the fundamental stage has
        found the series to be, if the waveform repeats there too: True when moved"""
        if not self.repeats_at(x, end, divisor):
            return False
        self.period /= divisor
        self.rate, self.slew = 0.0, 0.0
        self.smooth, self.lag = math.log2(self.frequency), 0.0
        return True

    def _miss(self):
        if self.coast_s == 0.0:
            self.last_period, self.lost_s = self.period, 0.0
        self.coast_s += self.cfg.hop_s
        self.corr = 0.0
        if self.coast_s > self.cfg.hold_s:
            self.period = 0.0
            self.candidates = []

    def update(self, x, end, silent):
        self.relocked = False
        if not self.locked or self.coast_s > 0.0:
            self.lost_s += self.cfg.hop_s
        if silent:
            if self.locked:
                self._miss()
            else:
                self.candidates = []
        elif self.locked:
            self._track(x, end)
            if self.coast_s > 0.0:
                # coasting through something loud: a slip to a new note, or a burst; look for the
                # new note meanwhile, and take it as soon as consecutive frames agree on it
                self._acquire(x, end)
        else:
            self._acquire(x, end)
        if self.locked:
            self._smooth()
        else:
            self.smooth = 0.0


def _floors_db(db, reach, shoulder):
    """the floor beside every bin: the lower of the medians of shoulder bins on each side,
    past the lobe; a side cut short by the spectrum's edge uses the bins it has, down to
    three; -inf where neither side has that many"""
    n = db.shape[0]
    if n < shoulder:
        return np.full(n, -np.inf)
    medians = np.median(np.lib.stride_tricks.sliding_window_view(db, shoulder), axis=1)   # medians[i]: bins i .. i+shoulder-1
    left = np.full(n, np.inf)
    right = np.full(n, np.inf)
    idx = np.arange(n)
    start = idx - reach - shoulder
    ok = start >= 0
    left[ok] = medians[start[ok]]
    for p in np.where(~ok & (idx - reach >= 3))[0]:      # a left shoulder cut short by the first bin
        left[p] = np.median(db[:p - reach])
    start = idx + reach + 1
    ok = start + shoulder <= n
    right[ok] = medians[start[ok]]
    for p in np.where(~ok & (n - (idx + reach + 1) >= 3))[0]:
        right[p] = np.median(db[p + reach + 1:])
    floor = np.minimum(left, right)
    floor[np.isinf(floor)] = -np.inf
    return floor


class Fundamental:
    def __init__(self, cfg, fs):
        self.cfg, self.fs = cfg, fs
        self.decision = None       # (g, m): the fundamental is the lock times g over m
        self.committed = False
        self.harmonicity = 0.0
        self.pending = None        # a decision seen but not yet held long enough
        self.pending_count = 0
        self.low_s = 0.0           # time a committed decision has spent below harmonicity_off
        self.ratio = 0.0           # the fit's fundamental over the lock's comb fundamental, 0 for none...
        self.fit_peaks = 0         # ...and the peaks it rests on
        self.correction = 1.0
        self.sub_seen = {}         # per sub-multiple, the leaky fraction of recent evaluations with evidence
        self.lift_seen = {}        # per divisor, the same
        self.last = None           # what the last evaluation found, for debugging
        self._windows = {}

    def reset(self):
        self.decision, self.committed, self.pending, self.pending_count = None, False, None, 0
        self.harmonicity, self.ratio, self.correction, self.low_s = 0.0, 0.0, 1.0, 0.0
        self.sub_seen, self.lift_seen = {}, {}

    def _window(self, n):
        """the window for a frame length, summing to the length, and the bias of a parabola
        through the peak of its transform, as the readout oracle tabulates it"""
        if n not in self._windows:
            w = _blackman_harris(n)
            w = w * (n / np.sum(w))
            self._windows[n] = (w, oracle.correction_table(w))
        return self._windows[n]

    def _frame(self, x, end, f_lock, history):
        """the spectral frame, warped by the lock's history to a constant pitch: the
        samples, and the pitch they now have. The history is the lock's stamped periods
        and its rate of change, which carries it on to the frame's end, past the last stamp."""
        cfg = self.cfg
        n = int(cfg.cycles_in_frame * self.fs / f_lock)
        n = 1 << max(int(math.ceil(math.log2(max(n, 2)))), 1)
        n = int(min(max(n, cfg.frame_min), cfg.frame_max))
        centres, periods, rate = history
        if end < n:
            # before a whole frame of signal exists, silence stands in for what came before it
            pad = n - end
            x = np.concatenate([np.zeros(pad), x[:end]])
            centres = centres + pad
            end = n
        t = np.arange(end - n, end, dtype=float)
        sel = (centres >= end - n - self.fs * 0.05) & (periods > 0.0)
        if np.sum(sel) >= 2:
            c, p = centres[sel], periods[sel]
            if end > c[-1]:
                hop = cfg.hop_s * self.fs
                c = np.append(c, float(end))
                p = np.append(p, p[-1] * (1.0 + rate * (end - c[-2]) / hop))
            p = np.interp(t, c, p)
            phase = np.cumsum(1.0 / p)
            phase -= phase[0]
            cycles = float(phase[-1])
            t_u = np.interp(np.linspace(0.0, cycles, n), phase, t)
            frame = np.interp(t_u, t, x[end - n:end])
            f_ref = cycles * self.fs / (n - 1)
        else:
            frame = x[end - n:end]
            f_ref = f_lock
        return frame, f_ref

    def _peaks(self, frame):
        """the prominent peaks of the frame: frequency, power, prominence; each refined by a
        parabola through its neighbours less the window's bias in it"""
        cfg = self.cfg
        n = frame.shape[0]
        window, table = self._window(n)
        spec = np.abs(np.fft.rfft(frame * window)) * 2.0 / n
        db = 20.0 * np.log10(spec + 1e-12)
        bin_hz = self.fs / n
        strongest = np.max(db[1:])
        floors = _floors_db(db, cfg.lobe_reach, cfg.shoulder)
        cand = np.where((db[1:-1] > db[:-2]) & (db[1:-1] >= db[2:]) & (db[1:-1] > strongest - cfg.peak_floor_db)
                        & (db[1:-1] - floors[1:-1] >= cfg.prominence_db))[0] + 1
        freqs, powers, proms = [], [], []
        for p in cand:
            shift, level = oracle.corrected(*_parabola(db[p - 1], db[p], db[p + 1]), table)
            f = (p + shift) * bin_hz
            if f < cfg.f_min * 0.5 or f > self.fs * 0.45:
                continue
            freqs.append(f)
            powers.append(10.0 ** (level / 10.0))
            proms.append(db[p] - floors[p])
        return np.array(freqs), np.array(powers), np.array(proms), bin_hz

    def _on_comb(self, freqs, f0, bin_hz, f_cap, coarse=False):
        """which peaks lie on the harmonics of f0 up to f_cap, and their numbers"""
        cfg = self.cfg
        k = np.round(freqs / f0)
        if coarse:
            tol = cfg.coarse_fraction * freqs
        else:
            b = cfg.inharmonicity_max * f0 / MIDDLE_C_HZ
            stretch = np.minimum(np.sqrt(1.0 + b * k * k) - 1.0, cfg.slot_fraction / np.maximum(k, 1.0))
            tol = cfg.on_comb_bins * bin_hz + (cfg.on_comb_fraction + stretch) * freqs
        on = (k >= 1) & (freqs <= f_cap) & (np.abs(freqs - k * f0) <= tol)
        return on, k

    def _refined(self, freqs, powers, f0, bin_hz, f_cap):
        """f0 refined by the lowest harmonics on its coarse comb, weighted by power"""
        on, k = self._on_comb(freqs, f0, bin_hz, f_cap, coarse=True)
        low = on & (k <= self.cfg.refine_harmonics)
        if not np.any(low):
            return f0
        kk, ff, ww = k[low], freqs[low], powers[low]
        return float(np.sum(ww * kk * ff) / np.sum(ww * kk * kk))

    def _two_voices(self, k, powers, on):
        """Two voices sharing the comb: every harmonic found is a multiple of one of two
        coprime numbers above 1, one of them with at least two harmonics, and nothing else.
        Returns the multiple of the louder voice, or 1."""
        ks = k[on].astype(int)
        pw = powers[on]
        if ks.size < 3:
            return 1
        best, best_power = 1, 0.0
        for a in self.cfg.voices:
            for b in self.cfg.voices:
                if b <= a or math.gcd(a, b) != 1:
                    continue
                in_a, in_b = ks % a == 0, ks % b == 0
                if not np.all(in_a | in_b) or not np.any(in_a) or not np.any(in_b) or max(np.sum(in_a), np.sum(in_b)) < 2:
                    continue
                pa, pb = float(np.sum(pw[in_a])), float(np.sum(pw[in_b]))
                louder, power = (a, pa) if pa >= pb else (b, pb)
                if power > best_power:
                    best, best_power = louder, power
        return best

    def evaluate(self, x, end, f_lock, history, repeats_at):
        """one evaluation; repeats_at(divisor) says whether the waveform repeats at that
        divisor of the lock's period"""
        cfg = self.cfg
        frame, f_ref = self._frame(x, end, f_lock, history)
        if frame is None:
            return
        freqs, powers, proms, bin_hz = self._peaks(frame)
        if freqs.size == 0:
            self.harmonicity = 0.0
            return self._observe(None)
        strongest = float(np.max(powers))
        f_cap = cfg.comb_span * f_ref
        # the peaks that count as evidence of a series: prominent, and within the balance of the strongest
        evident = (proms >= cfg.evidence_prominence_db) & (powers >= strongest * 10.0 ** (-cfg.balance_db / 10.0))
        total = float(np.sum(powers[evident]))

        # the lock's comb, refined, and the divisor that lifts a lock sitting below the fundamental:
        # the greatest common divisor of the slots holding evidence
        f1 = self._refined(freqs, powers, f_ref, bin_hz, f_cap)
        on1, k1 = self._on_comb(freqs, f1, bin_hz, f_cap)
        explained1 = float(np.sum(powers[on1]))
        g = int(np.gcd.reduce(k1[on1 & evident].astype(int))) if np.any(on1 & evident) else 1
        g = max(g, 1)
        # A lift, like a sub-multiple, must persist: the first frames of a note show its strongest
        # partial alone. And the waveform must repeat at the shorter period the lift claims: a lone
        # partial's number is otherwise no series, while a lock sitting on a multiple of a sine's
        # period is lifted by that one partial.
        building = False
        for d in set(self.lift_seen) | ({g} if g > 1 else set()):
            seen = self.lift_seen.get(d, 0.0)
            seen += cfg.persistence_leak * ((1.0 if d == g else 0.0) - seen)
            self.lift_seen[d] = seen
        if g > 1 and self.lift_seen[g] < cfg.persistence:
            building = self.lift_seen[g] >= cfg.pending
            g = 1
        elif g > 1 and not repeats_at(g):
            g = 1
        best = (g, 1, f1 * g)
        best_explained = explained1
        for m in cfg.sub_multiples:
            f0 = self._refined(freqs, powers, f_ref / m, bin_hz, f_cap)
            evidence = False
            if f0 >= cfg.f_min * 0.98:
                on, k = self._on_comb(freqs, f0, bin_hz, f_cap)
                # the slots the lock's comb shares are judged by it; the sub-multiple's own slots are its evidence
                own = on & (k % m != 0)
                evidence = bool(np.any(own & evident & (freqs < cfg.evidence_span * f_ref)))
            # evidence must persist: a noise peak lands on an extra slot now and then, a sub-oscillator every time
            seen = self.sub_seen.get(m, 0.0)
            seen += cfg.persistence_leak * ((1.0 if evidence else 0.0) - seen)
            self.sub_seen[m] = seen
            if evidence and seen >= cfg.persistence:
                explained = float(np.sum(powers[on1 | own]))
                if explained > best_explained * 1.0001:
                    best, best_explained = (1, m, f0), explained
            elif evidence and seen >= cfg.pending:
                building = True
        g, m, f0 = best
        if g == 1 and m == 1 and building and not self.committed:
            # a sub-multiple or a lift may be about to win: the lock's own comb does not commit meanwhile
            self.harmonicity = 0.0
            self.last = dict(f_ref=f_ref, f1=f1, g=g, m=m, harmonicity=0.0, ratio=0.0, peaks=int(freqs.size), on=0,
                             sub_seen=dict(self.sub_seen), building=True)
            return
        # the peaks on the chosen comb, over its own span: a lifted comb reaches further than the lock's
        on, k = self._on_comb(freqs, f0, bin_hz, cfg.comb_span * max(f0, f_ref))
        voice = self._two_voices(k, powers, on)
        if voice > 1:
            g, f0 = g * voice, f0 * voice
            on, k = self._on_comb(freqs, f0, bin_hz, cfg.comb_span * f0)
        self.harmonicity = float(np.sum(powers[on & evident]) / total) if total > 0.0 else 0.0
        self.ratio = self._fit(freqs[on], k[on], powers[on], f_ref * g / m)
        self.fit_peaks = int(np.sum(on))
        self.last = dict(f_ref=f_ref, f1=f1, g=g, m=m, harmonicity=self.harmonicity, ratio=self.ratio,
                         peaks=int(freqs.size), on=int(np.sum(on)), sub_seen=dict(self.sub_seen))
        return self._observe((g, m))

    def _fit(self, ff, kk, ww, base):
        """The fundamental of the partials on the comb, over base: a fit of
        (f_k / k)^2 = f0^2 (1 + B k^2), a stretched string's law, weighted by power, so
        that f0 is the lowest partial's frequency whatever the stretch. With one
        partial, or a fit that makes no sense, the plain weighted fit of f_k = k f0."""
        if ff.size == 0:
            return 0.0
        if ff.size >= 3:
            y = (ff / kk) ** 2
            u = kk * kk
            w = ww / np.sum(ww)
            um, ym = float(np.sum(w * u)), float(np.sum(w * y))
            var = float(np.sum(w * (u - um) ** 2))
            if var > 0.0:
                slope = float(np.sum(w * (u - um) * (y - ym))) / var
                f0sq = ym - slope * um
                if f0sq > 0.0 and slope >= -1e-9 * f0sq:
                    return math.sqrt(f0sq) / base
        return float(np.sum(ww * kk * ff) / np.sum(ww * kk * kk)) / base

    def _observe(self, decision):
        cfg = self.cfg
        dt = cfg.hop_s
        if decision is None or self.harmonicity < cfg.harmonicity_off:
            # nothing to go on: a committed decision holds, and lapses only after a while
            self.pending, self.pending_count = None, 0
            if self.committed:
                self.low_s += dt
                if self.low_s > cfg.lapse_s:
                    self.committed, self.decision, self.low_s = False, None, 0.0
            return
        self.low_s = 0.0
        if not self.committed and self.harmonicity < cfg.harmonicity_on:
            self.pending, self.pending_count = None, 0
            return
        if self.committed:
            if decision == self.decision:
                # an evaluation for the committed decision sets a pending revision back a step, not to nothing:
                # a stray peak on the comb now and then must not keep a wrong decision in place
                self.pending_count = max(0, self.pending_count - 1)
                if self.pending_count == 0:
                    self.pending = None
            else:
                if decision == self.pending:
                    self.pending_count += 1
                else:
                    self.pending, self.pending_count = decision, 1
                # a sub-multiple's or a lift's evidence has already persisted; it replaces the lock's own
                # comb as quickly as a first decision commits
                needed = cfg.commit_evals if decision != (1, 1) and self.decision == (1, 1) else cfg.revise_evals
                if self.pending_count >= needed:
                    self.decision, self.pending, self.pending_count = decision, None, 0
                    self.correction = self._seed()
        else:
            if decision == self.pending:
                self.pending_count += 1
            else:
                self.pending, self.pending_count = decision, 1
            if self.pending_count >= cfg.commit_evals:
                self.decision, self.committed, self.pending, self.pending_count = decision, True, None, 0
                self.correction = self._seed()

    def _target(self):
        """the fit's ratio, within the correction allowed; 1 without a fit"""
        if self.ratio <= 0.0:
            return 1.0
        return min(max(self.ratio, 1.0 - self.cfg.correction_max), 1.0 + self.cfg.correction_max)

    def _seed(self):
        """where a fresh decision's correction starts: at the fit when it rests on enough peaks
        to be trusted, at none otherwise"""
        return self._target() if self.fit_peaks >= self.cfg.seed_peaks else 1.0

    def value(self, f_lock, dt):
        """the reported fundamental: the lock over its ratio, corrected toward the fit"""
        if not self.committed or f_lock <= 0.0:
            return 0.0
        g, m = self.decision
        base = f_lock * g / m
        if self.ratio > 0.0:
            alpha = 1.0 - math.exp(-dt / self.cfg.correction_tc_s)
            self.correction += alpha * (self._target() - self.correction)
        return base * self.correction


def estimate(audio, fs, cfg=None):
    cfg = cfg or Config()
    x = audio.astype(np.float64)
    hop = int(cfg.hop_s * fs)
    lock = Lock(cfg, fs)
    fund = Fundamental(cfg, fs)
    gate = 10.0 ** (cfg.gate_dbfs / 20.0)
    hops = list(range(hop, x.shape[0] + 1, hop))
    centres = np.zeros(len(hops))   # the sample each hop's period describes
    periods = np.zeros(len(hops))
    times, lock_hz, lock_conf, f0_hz, committed, debug, delays = [], [], [], [], [], [], []
    since = 0   # the hop the current lock began at
    for i, end in enumerate(hops):
        recent = x[max(0, end - hop * 4):end]
        silent = float(np.sqrt(np.mean(recent * recent))) < gate
        lock.update(x, end, silent)
        delay = lock.centre if lock.locked else cfg.window_min_s * fs / 2.0
        periods[i] = lock.period
        centres[i] = end - delay if i == 0 else max(end - delay, centres[i - 1] + 1.0)   # a search's stamp can
        delay = end - centres[i]                                                         # trail the last hop's
        fund.last = None
        if not lock.locked or lock.relocked:
            fund.reset()
            since = i
        if lock.locked and lock.corr > 0.0:
            fund.evaluate(x, end, lock.frequency, (centres[:i + 1], periods[:i + 1], lock.rate),
                          lambda divisor: lock.repeats_at(x, end, divisor))
            g, m = fund.decision if fund.committed else (1, 1)
            if g > 1 and lock.retune(x, end, g):
                # the lock moves down to the fundamental's series; its history and the decision follow
                periods[since:i + 1] /= g
                fund.decision = (1, m)
        f_lock = lock.reported
        f0 = fund.value(f_lock, cfg.hop_s)
        times.append((end - delay) / fs)
        if lock.locked:
            delays.append(delay / fs)
        lock_hz.append(f_lock)
        lock_conf.append(lock.corr if lock.locked else 0.0)
        f0_hz.append(f0)
        committed.append(fund.committed and lock.locked)
        debug.append(dict(fund.last or {}, decision=fund.decision, committed=fund.committed, coast=lock.coast_s))
    traj = Trajectory(np.array(times), np.array(lock_hz), np.array(lock_conf), np.array(f0_hz), np.array(committed),
                      latency_s=float(np.median(delays)) if delays else cfg.window_min_s / 2.0)
    traj.debug = debug   # per hop, what the stages saw
    return traj
