"""The generative engine behind the tracker's corpus: a recipe, a point in the space
objective.md lays out, becomes audio and its truth. Everything is deterministic in
the recipe's seed. The pitch trajectory, the timbre, the amplitude envelope, the
nuisances and the realism layer are separate, so any of them combines with any other.

A recipe is a dict:

    fs, seconds
    pitch:     {kind: static|vibrato|glide|sweep|steps|pluck|lfo, ...}
    timbre:    {kind: sine|saw|square|pulse|formant|absent|sub|stretched|partials|filtered, ...}
    amplitude: {level_db, envelope: sustain|pluck|tremolo|gated, ...}
    timing:    {onset_s, offset_s, attack_ms, release_ms}
    realism:   {jitter_cents, drift_cents_per_s, random_phase, am_noise_db, onset_noise_db, floor_db, bandwidth_hz}
    nuisance:  [{kind: noise|hum|clicks|room|chorus|second_voice|dropout|burst, ...}, ...]
    negative:  {kind: ringmod|fm|noise|silence, ...}     (replaces pitch and timbre)
    seed

The truth is the fundamental's trajectory on a 1 ms grid with a voiced flag and a
tolerance in cents, the note onsets, the glitches, and the partials the lock stage can
hold on to. Jitter is excluded from the truth and counted as tolerance; drift is in it.
"""
from __future__ import annotations

import json
import math
from dataclasses import dataclass, field, asdict

import numpy as np

from s9.recipes import formant_lift

VERSION = 1            # bumped when a change here alters what a recipe renders to, so cached vectors are remade
TRUTH_STEP_S = 0.001
MAX_PARTIALS = 120


@dataclass
class Truth:
    t: np.ndarray                  # seconds, 1 ms grid
    f0: np.ndarray                 # Hz, 0 where unvoiced
    voiced: np.ndarray             # bool
    tolerance_cents: np.ndarray    # allowed deviation on each step, beyond the promises' own
    onsets: list = field(default_factory=list)    # seconds: where a note starts, or restarts
    glitches: list = field(default_factory=list)  # (start_s, length_s): interruptions within a note
    partials: list = field(default_factory=list)  # (harmonic number, dB below the strongest) present
    notes: str = ""

    def save(self, path):
        np.savez(path, t=self.t, f0=self.f0, voiced=self.voiced, tolerance_cents=self.tolerance_cents,
                 meta=json.dumps({"onsets": self.onsets, "glitches": self.glitches, "partials": self.partials, "notes": self.notes}))

    @staticmethod
    def load(path):
        z = np.load(path, allow_pickle=False)
        meta = json.loads(str(z["meta"]))
        return Truth(z["t"], z["f0"], z["voiced"], z["tolerance_cents"], meta["onsets"], meta["glitches"], meta["partials"], meta["notes"])


def cents(f, ref):
    return 1200.0 * np.log2(f / ref)


# --- pitch: the fundamental in Hz per sample, and the onsets a trajectory implies

def pitch(spec, t, rng):
    kind = spec["kind"]
    f0 = float(spec.get("f0", 220.0))
    if kind == "static":
        return np.full(t.shape, f0), []
    if kind == "vibrato":
        depth, rate = spec["depth_cents"], spec["rate_hz"]
        return f0 * 2.0 ** (depth / 1200.0 * np.sin(2.0 * np.pi * rate * t + spec.get("phase", 0.0))), []
    if kind == "glide":
        f_end, start, dur = spec["f_end"], spec.get("start_s", 0.5), spec["duration_s"]
        u = np.clip((t - start) / dur, 0.0, 1.0)
        return f0 * (f_end / f0) ** u, []
    if kind == "sweep":
        # a triangle in log frequency between f0 and f_hi at a constant slew, up first
        f_hi, slew = spec["f_hi"], spec["slew_oct_per_s"]
        span = math.log2(f_hi / f0)
        phase = (slew * t) % (2.0 * span)
        octaves = np.where(phase < span, phase, 2.0 * span - phase)
        return f0 * 2.0 ** octaves, []
    if kind == "steps":
        # notes as (f0, duration_s) in turn, each an onset
        f = np.zeros(t.shape)
        onsets, at = [], 0.0
        for note_f0, dur in spec["notes"]:
            sel = (t >= at) & (t < at + dur)
            f[sel] = note_f0
            onsets.append(at)
            at += dur
        f[t >= at] = spec["notes"][-1][0]
        return f, onsets[1:]
    if kind == "pluck":
        # a pluck starts sharp and settles
        sharpen, tau = spec.get("sharpen_cents", 30.0), spec.get("tau_s", 0.1)
        return f0 * 2.0 ** (sharpen / 1200.0 * np.exp(-t / tau)), []
    if kind == "lfo":
        depth, rate, shape = spec["depth_cents"], spec["rate_hz"], spec.get("shape", "triangle")
        ph = (rate * t) % 1.0
        if shape == "triangle":
            wave = 4.0 * np.abs(ph - 0.5) - 1.0
        elif shape == "square":
            wave = np.where(ph < 0.5, 1.0, -1.0)
        elif shape == "sh":   # sample and hold: a new value each cycle, every step an onset
            steps = np.floor(rate * t).astype(int)
            values = rng.uniform(-1.0, 1.0, size=steps.max() + 1)
            wave = values[steps]
            onsets = [k / rate for k in range(1, steps.max() + 1)]
            return f0 * 2.0 ** (depth / 1200.0 * wave), onsets
        else:
            wave = np.sin(2.0 * np.pi * ph)
        return f0 * 2.0 ** (depth / 1200.0 * wave), []
    raise ValueError(f"unknown pitch kind {kind!r}")


# --- timbre: harmonic numbers, their stretch, their amplitudes as functions of time

def _lowpass_gain(f, fc, q):
    """a resonant two-pole low-pass's magnitude at f"""
    r = f / fc
    return 1.0 / np.sqrt((1.0 - r * r) ** 2 + (r / q) ** 2)


def timbre(spec, f0_ctl, t_ctl, fs, rng):
    """Returns (k, stretch, gains): harmonic numbers, the factor each is stretched by,
    and gains of shape (partials, control steps), relative, before the level is set.
    f0_ctl is the fundamental on the control grid t_ctl."""
    kind = spec["kind"]
    nyquist = 0.45 * fs
    f_min_ctl = float(np.min(f0_ctl[f0_ctl > 0])) if np.any(f0_ctl > 0) else 1.0
    kmax = int(min(MAX_PARTIALS, nyquist / f_min_ctl))
    kmax = max(kmax, 1)
    k = np.arange(1, kmax + 1, dtype=float)
    stretch = np.ones_like(k)
    steps = t_ctl.shape[0]

    if kind == "sine":
        k, a = np.array([1.0]), np.array([1.0])
    elif kind == "saw":
        a = 1.0 / k
    elif kind == "square":
        a = np.where(k % 2 == 1, 1.0 / k, 0.0)
    elif kind == "pulse":
        d0, dm, rate = spec.get("duty", 0.25), spec.get("duty_mod", 0.0), spec.get("rate_hz", 0.5)
        duty = d0 + dm * np.sin(2.0 * np.pi * rate * t_ctl)          # (steps,)
        gains = 2.0 * np.abs(np.sin(np.pi * k[:, None] * duty[None, :])) / (np.pi * k[:, None])
        return k, stretch, gains
    elif kind == "formant":
        f1, f2 = spec.get("f1", 700.0), spec.get("f2", 1800.0)
        q1, q2 = spec.get("q1", 120.0), spec.get("q2", 200.0)
        slope, h1_db = spec.get("slope_db", -12.0), spec.get("h1_db", -30.0)
        f_nom = float(np.median(f0_ctl[f0_ctl > 0]))
        fk = k * f_nom
        a = np.array([formant_lift(f, f1, f2, q1, q2) for f in fk]) * 2.0 ** (slope / 6.0 * np.log2(k))
        a[0] *= 10.0 ** (h1_db / 20.0)
    elif kind == "absent":
        a = 1.0 / k
        for m in spec.get("missing", [1]):
            if m <= kmax:
                a[m - 1] = 0.0
    elif kind == "sub":
        # the sub-oscillator an octave down is the physical fundamental: the series is of f0/2
        sub_db = spec.get("sub_db", -12.0)
        k = np.arange(1, 2 * kmax + 1, dtype=float)      # harmonics of f0/2
        stretch = np.ones_like(k)
        a = np.where(k % 2 == 0, 2.0 / k, 0.0)            # the main oscillator: a saw at f0
        a[0] = 10.0 ** (sub_db / 20.0)                    # the sub: a sine at f0/2
        gains = np.repeat(a[:, None], steps, axis=1)
        return k / 2.0, stretch, gains
    elif kind == "stretched":
        b = spec.get("inharmonicity", 1e-4)
        stretch = np.sqrt(1.0 + b * k * k)
        a = 1.0 / k
    elif kind == "partials":
        count = int(spec.get("count", 8))
        k = k[:count]
        stretch = stretch[:count]
        a = np.ones(count) if spec.get("flat", False) else 1.0 / k
    elif kind == "filtered":
        fc0, depth, rate, q = spec.get("cutoff_hz", 1000.0), spec.get("depth_oct", 2.0), spec.get("rate_hz", 0.5), spec.get("q", 4.0)
        fc = fc0 * 2.0 ** (depth * np.sin(2.0 * np.pi * rate * t_ctl))      # (steps,)
        fk = k[:, None] * f0_ctl[None, :]
        gains = _lowpass_gain(fk, fc[None, :], q) / k[:, None]
        return k, stretch, gains
    else:
        raise ValueError(f"unknown timbre kind {kind!r}")

    gains = np.repeat(a[:, None], steps, axis=1)
    return k, stretch, gains


# --- amplitude: the envelope on the control grid, and per-partial decays

def envelope(spec, timing, t_ctl, seconds):
    onset, offset = timing.get("onset_s", 0.0), timing.get("offset_s", seconds)
    attack, release = timing.get("attack_ms", 5.0) / 1000.0, timing.get("release_ms", 20.0) / 1000.0
    env = np.clip((t_ctl - onset) / max(attack, 1e-4), 0.0, 1.0) * np.clip((offset - t_ctl) / max(release, 1e-4), 0.0, 1.0)
    kind = spec.get("envelope", "sustain")
    onsets = []
    if kind == "tremolo":
        depth, rate = spec.get("depth", 0.5), spec.get("rate_hz", 4.0)
        env = env * (1.0 - depth / 2.0 + depth / 2.0 * np.cos(2.0 * np.pi * rate * t_ctl))
    elif kind == "gated":
        period, duty = spec.get("period_s", 0.5), spec.get("duty", 0.5)
        ph = ((t_ctl - onset) / period) % 1.0
        gate = (ph < duty).astype(float)
        env = env * gate
        onsets = [onset + n * period for n in range(1, int((seconds - onset) / period) + 1) if onset + n * period < offset]
    return env, onsets


def partial_decays(spec, k, t_ctl, timing):
    """per-partial multipliers for a plucked envelope: higher partials decay faster"""
    if spec.get("envelope") != "pluck":
        return None
    tau, slope = spec.get("tau_s", 0.8), spec.get("decay_slope", 0.15)
    onset = timing.get("onset_s", 0.0)
    tau_k = tau / (1.0 + slope * (k - 1.0))
    return np.exp(-np.maximum(t_ctl[None, :] - onset, 0.0) / tau_k[:, None])


# --- realism

def _lowpassed_noise(rng, n, fs, bandwidth_hz):
    """white noise low-passed by a one-pole to about bandwidth_hz, unit rms"""
    x = rng.standard_normal(n)
    a = math.exp(-2.0 * math.pi * bandwidth_hz / fs)
    y = np.empty(n)
    acc = 0.0
    b = 1.0 - a
    for i in range(n):   # a one-pole; n is a control-rate count, so this loop is short
        acc = a * acc + b * x[i]
        y[i] = acc
    return y / (np.std(y) + 1e-12)


def pink_noise(rng, n):
    """approximately 1/f noise: the Paul Kellet three-pole filter over white noise, unit rms"""
    w = rng.standard_normal(n)
    b0 = b1 = b2 = 0.0
    y = np.empty(n)
    for i in range(n):
        b0 = 0.99765 * b0 + w[i] * 0.0990460
        b1 = 0.96300 * b1 + w[i] * 0.2965164
        b2 = 0.57000 * b2 + w[i] * 1.0526913
        y[i] = b0 + b1 + b2 + w[i] * 0.1848
    return y / (np.std(y) + 1e-12)


def _brickwall(x, fs, hz):
    spectrum = np.fft.rfft(x)
    freqs = np.fft.rfftfreq(x.shape[0], 1.0 / fs)
    spectrum[freqs > hz] = 0.0
    return np.fft.irfft(spectrum, n=x.shape[0])


# --- the render

def _control_grid(seconds):
    return np.arange(0.0, seconds, TRUTH_STEP_S)


def render_voice(recipe, rng, detune_cents=0.0, f0_override=None):
    """The clean periodic signal of a recipe at unit peak, with its truth ingredients:
    (audio, f0 on the control grid, voiced on the control grid, tolerance, onsets, partials)."""
    fs, seconds = int(recipe.get("fs", 48000)), float(recipe.get("seconds", 3.0))
    n = int(seconds * fs)
    t = np.arange(n) / fs
    t_ctl = _control_grid(seconds)
    timing = recipe.get("timing", {})
    realism = recipe.get("realism", {})
    amp = recipe.get("amplitude", {})

    f0, onsets = pitch(recipe["pitch"], t, rng)
    if f0_override is not None:
        f0 = f0_override(f0)
    f0 = f0 * 2.0 ** (detune_cents / 1200.0)

    # drift is real and in the truth; jitter is real and counted as tolerance
    drift = realism.get("drift_cents_per_s", 0.0) * t
    f0 = f0 * 2.0 ** (drift / 1200.0)
    jitter_cents = realism.get("jitter_cents", 0.0)
    tolerance = np.full(t_ctl.shape, 2.0 * jitter_cents)
    f0_played = f0
    if jitter_cents > 0.0:
        jit_ctl = _lowpassed_noise(rng, t_ctl.shape[0], 1.0 / TRUTH_STEP_S, 30.0) * jitter_cents
        f0_played = f0 * 2.0 ** (np.interp(t, t_ctl, jit_ctl) / 1200.0)

    f0_ctl = np.interp(t_ctl, t, f0)
    k, stretch, gains = timbre(recipe["timbre"], f0_ctl, t_ctl, fs, rng)
    env, gate_onsets = envelope(amp, timing, t_ctl, seconds)
    onsets = sorted(set([timing.get("onset_s", 0.0)] + list(onsets) + gate_onsets))
    decays = partial_decays(amp, k, t_ctl, timing)
    if decays is not None:
        gains = gains * decays
    am_db = realism.get("am_noise_db", 0.0)

    phase = 2.0 * np.pi * np.cumsum(f0_played) / fs
    thetas = rng.uniform(0.0, 2.0 * np.pi, size=k.shape[0]) if realism.get("random_phase", True) else np.zeros(k.shape[0])
    y = np.zeros(n)
    nyquist = 0.45 * fs
    present = []
    for i in range(k.shape[0]):
        g = gains[i]
        if not np.any(g > 0.0):
            continue
        fk_max = float(np.max(f0_ctl[f0_ctl > 0])) * k[i] * stretch[i] if np.any(f0_ctl > 0) else 0.0
        if fk_max > nyquist:
            continue
        if am_db > 0.0:
            g = g * 10.0 ** (_lowpassed_noise(rng, t_ctl.shape[0], 1.0 / TRUTH_STEP_S, 20.0) * am_db / 20.0)
        g_t = np.interp(t, t_ctl, g * env)
        y += g_t * np.sin(k[i] * stretch[i] * phase + thetas[i])
        present.append((float(k[i]), float(np.max(gains[i]))))

    strongest = max(a for _, a in present) if present else 1.0
    partials = [(kk, round(20.0 * math.log10(a / strongest), 1)) for kk, a in present if a > 0.0]
    if recipe["timbre"]["kind"] == "sub":
        # the sub-oscillator is the physical fundamental: the truth is its frequency, the partials its harmonics
        f0_ctl = f0_ctl / 2.0
        partials = [(kk * 2.0, db) for kk, db in partials]

    peak = float(np.max(np.abs(y))) if n else 0.0
    if peak > 0.0:
        y = y / peak

    # onset noise: a short burst at each onset, as a pick or a breath
    onset_db = realism.get("onset_noise_db", None)
    if onset_db is not None:
        for at in onsets:
            i0, i1 = int(at * fs), int((at + 0.02) * fs)
            if i1 <= n:
                burst = rng.standard_normal(i1 - i0) * np.linspace(1.0, 0.0, i1 - i0)
                y[i0:i1] += burst * 10.0 ** (onset_db / 20.0)

    voiced = env > 1e-3
    return y, f0_ctl, voiced, tolerance, onsets, partials


def render_negative(spec, fs, seconds, rng):
    n = int(seconds * fs)
    t = np.arange(n) / fs
    kind = spec["kind"]
    if kind == "silence":
        return np.zeros(n)
    if kind == "noise":
        return pink_noise(rng, n) if spec.get("color", "white") == "pink" else rng.standard_normal(n)
    if kind == "ringmod":
        f1, f2 = spec.get("f1", 440.0), spec.get("f2", 611.0)
        return np.sin(2.0 * np.pi * f1 * t) * np.sin(2.0 * np.pi * f2 * t)
    if kind == "fm":
        fc, ratio, index = spec.get("carrier_hz", 440.0), spec.get("ratio", math.sqrt(2.0)), spec.get("index", 3.0)
        return np.sin(2.0 * np.pi * fc * t + index * np.sin(2.0 * np.pi * fc * ratio * t))
    raise ValueError(f"unknown negative kind {kind!r}")


def render(recipe):
    """Audio (float32 at unit peak at most) and Truth for a recipe."""
    rng = np.random.default_rng(int(recipe.get("seed", 0)))
    fs, seconds = int(recipe.get("fs", 48000)), float(recipe.get("seconds", 3.0))
    n = int(seconds * fs)
    t = np.arange(n) / fs
    t_ctl = _control_grid(seconds)
    realism = recipe.get("realism", {})
    amp = recipe.get("amplitude", {})
    level = 10.0 ** (amp.get("level_db", -6.0) / 20.0)
    glitches = []
    notes = []

    if "negative" in recipe:
        y = render_negative(recipe["negative"], fs, seconds, rng) * level
        y = y / max(1.0, float(np.max(np.abs(y))) / level) if np.any(y) else y
        truth = Truth(t_ctl, np.zeros(t_ctl.shape), np.zeros(t_ctl.shape, dtype=bool), np.zeros(t_ctl.shape),
                      [], [], [], recipe["negative"]["kind"])
        return y.astype(np.float32), truth

    y, f0_ctl, voiced, tolerance, onsets, partials = render_voice(recipe, rng)
    y = y * level
    signal_rms = float(np.sqrt(np.mean(y[np.interp(t, t_ctl, voiced.astype(float)) > 0.5] ** 2))) if np.any(voiced) else 1e-6

    for item in recipe.get("nuisance", []):
        kind = item["kind"]
        if kind == "noise":
            noise = pink_noise(rng, n) if item.get("color", "white") == "pink" else rng.standard_normal(n)
            y = y + noise * signal_rms * 10.0 ** (-item["snr_db"] / 20.0)
        elif kind == "hum":
            f, db = item.get("hz", 50.0), item.get("level_db", -40.0)
            hum = np.sin(2.0 * np.pi * f * t) + 0.5 * np.sin(2.0 * np.pi * 2 * f * t) + 0.25 * np.sin(2.0 * np.pi * 3 * f * t)
            y = y + hum * 10.0 ** (db / 20.0)
        elif kind == "clicks":
            count, db = int(item.get("count", 10)), item.get("level_db", -6.0)
            for i in rng.integers(0, n, size=count):
                y[i] += 10.0 ** (db / 20.0) * rng.choice([-1.0, 1.0])
        elif kind == "room":
            rt60, wet = item.get("rt60_s", 0.8), item.get("wet", 0.3)
            m = int(rt60 * fs)
            ir = rng.standard_normal(m) * np.exp(-6.9078 * np.arange(m) / m)
            ir[0] = 0.0
            ir = ir / (np.sqrt(np.sum(ir * ir)) + 1e-12)
            size = 1 << (n + m - 1).bit_length()
            tail = np.fft.irfft(np.fft.rfft(y, size) * np.fft.rfft(ir, size), size)[:n]
            y = (1.0 - wet) * y + wet * tail
        elif kind == "chorus":
            depth, rate, mix_db = item.get("depth_cents", 10.0), item.get("rate_hz", 0.0), item.get("mix_db", -3.0)
            rng2 = np.random.default_rng(int(recipe.get("seed", 0)) + 1)
            det = depth * np.sin(2.0 * np.pi * rate * t) if rate > 0.0 else np.full(t.shape, depth)
            y2, _, _, _, _, _ = render_voice(recipe, rng2, f0_override=lambda f: f * 2.0 ** (det / 1200.0))
            y = y + y2 * level * 10.0 ** (mix_db / 20.0)
            det_ctl = np.interp(t_ctl, t, det)
            f0_ctl = f0_ctl * 2.0 ** (det_ctl / 2400.0)         # the truth is the mean of the two
            tolerance = tolerance + np.abs(det_ctl) / 2.0        # the beat is tolerance
            notes.append(f"chorus {depth} cents at {rate} Hz")
        elif kind == "second_voice":
            interval, mix_db = item.get("interval_semitones", 7.0), item.get("mix_db", -6.0)
            rng2 = np.random.default_rng(int(recipe.get("seed", 0)) + 2)
            y2, _, _, _, _, _ = render_voice(recipe, rng2, detune_cents=100.0 * interval)
            y = y + y2 * level * 10.0 ** (mix_db / 20.0)     # quieter than the main, whose truth stands
            notes.append(f"second voice {interval} semitones at {mix_db} dB")
        elif kind == "dropout":
            start, length = item["start_s"], item["length_s"]
            i0, i1 = int(start * fs), int((start + length) * fs)
            fade = max(1, int(0.001 * fs))
            y[i0:i1] = 0.0
            y[max(0, i0 - fade):i0] *= np.linspace(1.0, 0.0, min(fade, i0))
            y[i1:min(n, i1 + fade)] *= np.linspace(0.0, 1.0, min(fade, n - i1))
            glitches.append((start, length))
        elif kind == "burst":
            start, length, db = item["start_s"], item["length_s"], item.get("level_db", -3.0)
            i0, i1 = int(start * fs), int((start + length) * fs)
            y[i0:i1] += rng.standard_normal(i1 - i0) * 10.0 ** (db / 20.0)
            glitches.append((start, length))
        else:
            raise ValueError(f"unknown nuisance kind {kind!r}")

    floor_db = realism.get("floor_db", None)
    if floor_db is not None:
        y = y + rng.standard_normal(n) * 10.0 ** (floor_db / 20.0)
    bandwidth = realism.get("bandwidth_hz", None)
    if bandwidth is not None:
        y = _brickwall(y, fs, bandwidth)

    peak = float(np.max(np.abs(y))) if n else 0.0
    if peak > 1.0:
        y = y / peak
        notes.append(f"scaled by {1.0 / peak:.3f} to fit")

    f0_truth = np.where(voiced, f0_ctl, 0.0)
    truth = Truth(t_ctl, f0_truth, voiced, tolerance, [float(o) for o in onsets], glitches, partials, "; ".join(notes))
    return y.astype(np.float32), truth
