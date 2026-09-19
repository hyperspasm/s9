"""The corpus: recipes over the axes of objective.md, in three sets. The grid crosses
pitch, timbre and noise with the other axes fixed, so that failures show as regions;
the random set samples every axis at once from a seed; the named set encodes
particular promises. Each recipe carries tags, its position on the axes, for the
report to aggregate by. Audio and truth are generated on demand into a cache and
never committed; a cached vector is remade when its recipe changes.
"""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path

import numpy as np

from . import synth

CACHE = Path(__file__).resolve().parent / "cache"

NOTES = {"C1": 32.703, "C2": 65.406, "C3": 130.81, "C4": 261.63, "C5": 523.25, "C6": 1046.5, "C7": 2093.0}
GRID_PITCHES = list(NOTES)
GRID_TIMBRES = {
    "sine": {"kind": "sine"},
    "saw": {"kind": "saw"},
    "square": {"kind": "square"},
    "pulse": {"kind": "pulse", "duty": 0.2, "duty_mod": 0.15, "rate_hz": 0.7},
    "formant": {"kind": "formant", "h1_db": -30.0},
    "absent1": {"kind": "absent", "missing": [1]},
    "absent12": {"kind": "absent", "missing": [1, 2]},
    "sub-12": {"kind": "sub", "sub_db": -12.0},
    "stretched": {"kind": "stretched"},   # the inharmonicity is set per pitch, as a piano's rises with it
    "partials2": {"kind": "partials", "count": 2},
    "partials60": {"kind": "partials", "count": 60},
    "filtered": {"kind": "filtered", "cutoff_hz": 800.0, "depth_oct": 2.0, "rate_hz": 0.5, "q": 4.0},
}
GRID_NOISES = {
    "clean": [],
    "white30": [{"kind": "noise", "color": "white", "snr_db": 30.0}],
    "white10": [{"kind": "noise", "color": "white", "snr_db": 10.0}],
    "pink20": [{"kind": "noise", "color": "pink", "snr_db": 20.0}],
}
REALISM = {"jitter_cents": 0.5, "drift_cents_per_s": 0.0, "random_phase": True, "floor_db": -90.0}


def inharmonicity(f0):
    """a piano's, roughly: 2e-4 at middle C, rising with the pitch, within 3e-5 and 2e-3"""
    return float(min(2e-3, max(3e-5, 2e-4 * f0 / 261.63)))


def timbre_for(name, f0):
    spec = dict(GRID_TIMBRES[name])
    if spec["kind"] == "stretched":
        spec["inharmonicity"] = inharmonicity(f0)
    return spec


def recipe(id, tags, **fields):
    r = {"id": id, "tags": tags, "fs": 48000, "seconds": 3.0, "seed": _seed(id),
         "amplitude": {"level_db": -6.0, "envelope": "sustain"},
         "timing": {"onset_s": 0.1, "offset_s": 2.9, "attack_ms": 5.0},
         "realism": dict(REALISM), "nuisance": []}
    r.update(fields)
    return r


def _seed(id):
    return int.from_bytes(hashlib.sha256(id.encode()).digest()[:4], "little")


def grid():
    out = []
    for note in GRID_PITCHES:
        for tname, tspec in GRID_TIMBRES.items():
            for nname, nspec in GRID_NOISES.items():
                out.append(recipe(f"grid-{note}-{tname}-{nname}",
                                  {"set": "grid", "pitch": note, "timbre": tname, "noise": nname},
                                  pitch={"kind": "static", "f0": NOTES[note]}, timbre=timbre_for(tname, NOTES[note]), nuisance=list(nspec)))
    return out


def random_set(count=100, seed=7):
    rng = np.random.default_rng(seed)
    out = []
    for i in range(count):
        note = rng.choice(GRID_PITCHES)
        f0 = float(np.clip(NOTES[note] * 2.0 ** rng.uniform(-0.5, 0.5), NOTES["C1"], NOTES["C7"]))   # within the range promised
        pk = rng.choice(["static", "vibrato", "glide", "lfo", "pluck"])
        if pk == "static":
            pitch = {"kind": "static", "f0": f0}
        elif pk == "vibrato":
            pitch = {"kind": "vibrato", "f0": f0, "depth_cents": float(rng.uniform(5, 100)), "rate_hz": float(rng.uniform(0.5, 8))}
        elif pk == "glide":
            pitch = {"kind": "glide", "f0": f0, "f_end": f0 * 2.0 ** rng.uniform(-1, 1), "start_s": 1.0, "duration_s": float(rng.uniform(0.1, 1.5))}
        elif pk == "lfo":
            pitch = {"kind": "lfo", "f0": f0, "depth_cents": float(rng.uniform(5, 100)), "rate_hz": float(rng.uniform(0.5, 6)), "shape": str(rng.choice(["triangle", "square", "sine"]))}
        else:
            pitch = {"kind": "pluck", "f0": f0}
        tname = str(rng.choice(list(GRID_TIMBRES)))
        env = str(rng.choice(["sustain", "pluck", "tremolo", "gated"]))
        amplitude = {"level_db": float(rng.uniform(-40, -3)), "envelope": env}
        if env == "tremolo":
            amplitude.update({"depth": float(rng.uniform(0.2, 1.0)), "rate_hz": float(rng.uniform(1, 8))})
        if env == "gated":
            amplitude.update({"period_s": float(rng.uniform(0.3, 1.0)), "duty": 0.6})
        nuisance = []
        if rng.random() < 0.6:
            nuisance.append({"kind": "noise", "color": str(rng.choice(["white", "pink"])), "snr_db": float(rng.uniform(0, 40))})
        if rng.random() < 0.3:
            nuisance.append({"kind": "hum", "hz": float(rng.choice([50.0, 60.0])), "level_db": float(rng.uniform(-50, -30))})
        if rng.random() < 0.2:
            nuisance.append({"kind": "clicks", "count": int(rng.integers(2, 20))})
        if rng.random() < 0.3:
            nuisance.append({"kind": "room", "rt60_s": float(rng.uniform(0.3, 1.5)), "wet": float(rng.uniform(0.1, 0.5))})
        if rng.random() < 0.2:
            nuisance.append({"kind": "chorus", "depth_cents": float(rng.uniform(2, 30)), "rate_hz": float(rng.uniform(0, 1)), "mix_db": float(rng.uniform(-6, 0))})
        realism = {"jitter_cents": float(rng.uniform(0, 3)), "drift_cents_per_s": float(rng.uniform(-3, 3)),
                   "random_phase": True, "am_noise_db": float(rng.uniform(0, 2)), "floor_db": float(rng.uniform(-100, -60)),
                   "onset_noise_db": float(rng.uniform(-40, -20))}
        if rng.random() < 0.3:
            realism["bandwidth_hz"] = float(rng.uniform(4000, 12000))
        out.append(recipe(f"random-{i:03d}", {"set": "random", "pitch": note, "timbre": tname, "envelope": env, "pitch_kind": pk},
                          pitch=pitch, timbre=timbre_for(tname, f0), amplitude=amplitude, nuisance=nuisance, realism=realism,
                          timing={"onset_s": 0.1, "offset_s": 2.9, "attack_ms": float(rng.uniform(1, 200))}))
    return out


def named():
    out = []
    saw = {"kind": "saw"}
    # the fundamental that appears late: formant timbre, then the same with the fundamental present
    out.append(recipe("named-late-fundamental", {"set": "named", "case": "late-fundamental"}, seconds=5.0,
                      timing={"onset_s": 0.1, "offset_s": 4.9, "attack_ms": 5.0},
                      pitch={"kind": "static", "f0": 110.0}, timbre={"kind": "absent", "missing": [1, 2]},
                      nuisance=[{"kind": "second_voice", "interval_semitones": 0.0, "mix_db": 0.0}]))
    # glides over two octaves, both ways, at C2 and C5
    for note in ("C2", "C5"):
        for direction, octs in (("up", 2.0), ("down", -2.0)):
            out.append(recipe(f"named-glide-{note}-{direction}", {"set": "named", "case": "glide", "pitch": note}, seconds=4.0,
                              timing={"onset_s": 0.1, "offset_s": 3.9, "attack_ms": 5.0},
                              pitch={"kind": "glide", "f0": NOTES[note], "f_end": NOTES[note] * 2.0 ** octs, "start_s": 1.0, "duration_s": 2.0}, timbre=saw))
    # sweeps at rising slew until the lock breaks
    for note in ("C2", "C4"):
        for slew in (0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0):
            out.append(recipe(f"named-sweep-{note}-{slew:g}", {"set": "named", "case": "sweep", "pitch": note, "slew_oct_per_s": slew}, seconds=4.0,
                              timing={"onset_s": 0.1, "offset_s": 3.9, "attack_ms": 5.0},
                              pitch={"kind": "sweep", "f0": NOTES[note], "f_hi": NOTES[note] * 4.0, "slew_oct_per_s": slew}, timbre=saw))
    # vibrato at rates and depths
    for note in ("C2", "C4", "C6"):
        for rate in (1.0, 3.0, 5.0, 8.0):
            for depth in (20.0, 50.0, 100.0):
                out.append(recipe(f"named-vibrato-{note}-{rate:g}Hz-{depth:g}c", {"set": "named", "case": "vibrato", "pitch": note, "rate_hz": rate, "depth_cents": depth},
                                  pitch={"kind": "vibrato", "f0": NOTES[note], "depth_cents": depth, "rate_hz": rate}, timbre=saw))
    # dropouts and bursts at rising length
    for kind in ("dropout", "burst"):
        for ms in (5, 10, 20, 50, 100, 200, 300, 500):
            item = {"kind": kind, "start_s": 1.5, "length_s": ms / 1000.0}
            out.append(recipe(f"named-{kind}-{ms}ms", {"set": "named", "case": kind, "length_ms": ms},
                              pitch={"kind": "static", "f0": 220.0}, timbre=saw, nuisance=[item]))
    # a filter sweep that removes the partials the lock may start on
    out.append(recipe("named-filter-sweep", {"set": "named", "case": "filter-sweep"}, seconds=6.0,
                      timing={"onset_s": 0.1, "offset_s": 5.9, "attack_ms": 5.0},
                      pitch={"kind": "static", "f0": 110.0}, timbre={"kind": "filtered", "cutoff_hz": 2000.0, "depth_oct": 3.5, "rate_hz": 0.25, "q": 6.0}))
    # a note that stops and restarts an octave away
    out.append(recipe("named-restart-octave", {"set": "named", "case": "restart"}, seconds=4.0,
                      timing={"onset_s": 0.1, "offset_s": 3.9, "attack_ms": 5.0},
                      pitch={"kind": "steps", "notes": [(220.0, 1.9), (440.0, 2.1)]}, timbre=saw,
                      amplitude={"level_db": -6.0, "envelope": "gated", "period_s": 2.0, "duty": 0.9}))
    # steps of a scale, legato
    out.append(recipe("named-scale", {"set": "named", "case": "steps"}, seconds=4.0,
                      timing={"onset_s": 0.1, "offset_s": 3.9, "attack_ms": 5.0},
                      pitch={"kind": "steps", "notes": [(220.0 * 2.0 ** (s / 12.0), 0.5) for s in (0, 2, 4, 5, 7, 9, 11, 12)]}, timbre=saw))
    # sample-and-hold pitch: every step an onset
    out.append(recipe("named-sh", {"set": "named", "case": "sh"}, pitch={"kind": "lfo", "f0": 330.0, "depth_cents": 700.0, "rate_hz": 3.0, "shape": "sh"}, timbre=saw))
    # chorus and second voices
    for depth in (5.0, 15.0, 30.0):
        out.append(recipe(f"named-chorus-{depth:g}c", {"set": "named", "case": "chorus", "depth_cents": depth},
                          pitch={"kind": "static", "f0": 220.0}, timbre=saw, nuisance=[{"kind": "chorus", "depth_cents": depth, "rate_hz": 0.3, "mix_db": 0.0}]))
    for interval in (7.0, 12.0, 3.0):
        out.append(recipe(f"named-second-voice-{interval:g}st", {"set": "named", "case": "second-voice", "interval_semitones": interval},
                          pitch={"kind": "static", "f0": 220.0}, timbre=saw, nuisance=[{"kind": "second_voice", "interval_semitones": interval, "mix_db": -6.0}]))
    # a sub-oscillator at depths: the truth is the sub, the physical fundamental
    for sub_db in (-6.0, -12.0, -20.0, -30.0):
        out.append(recipe(f"named-sub{sub_db:g}dB", {"set": "named", "case": "sub", "sub_db": sub_db},
                          pitch={"kind": "static", "f0": 220.0}, timbre={"kind": "sub", "sub_db": sub_db}))
    # plucks, tremolo, a room
    out.append(recipe("named-pluck", {"set": "named", "case": "pluck"}, pitch={"kind": "pluck", "f0": 196.0}, timbre=saw,
                      amplitude={"level_db": -6.0, "envelope": "pluck", "tau_s": 0.8, "decay_slope": 0.15}, realism=dict(REALISM, onset_noise_db=-20.0)))
    out.append(recipe("named-tremolo", {"set": "named", "case": "tremolo"}, pitch={"kind": "static", "f0": 440.0}, timbre=saw,
                      amplitude={"level_db": -6.0, "envelope": "tremolo", "depth": 1.0, "rate_hz": 6.0}))
    out.append(recipe("named-room", {"set": "named", "case": "room"}, pitch={"kind": "static", "f0": 220.0}, timbre=saw,
                      nuisance=[{"kind": "room", "rt60_s": 1.2, "wet": 0.5}]))
    out.append(recipe("named-hum", {"set": "named", "case": "hum"}, pitch={"kind": "static", "f0": 220.0}, timbre=saw,
                      nuisance=[{"kind": "hum", "hz": 60.0, "level_db": -30.0}]))
    # negatives
    for neg in ({"kind": "ringmod", "f1": 440.0, "f2": 611.0}, {"kind": "fm", "carrier_hz": 440.0, "ratio": math.sqrt(2.0), "index": 3.0},
                {"kind": "noise", "color": "white"}, {"kind": "noise", "color": "pink"}, {"kind": "silence"}):
        name = neg["kind"] + ("-" + neg["color"] if "color" in neg else "")
        r = recipe(f"named-negative-{name}", {"set": "named", "case": "negative", "negative": name}, negative=neg)
        r.pop("realism")
        out.append(r)
    return out


SETS = {"grid": grid, "random": random_set, "named": named}


def recipes(which="all"):
    names = list(SETS) if which == "all" else which.split(",")
    out = []
    for name in names:
        out.extend(SETS[name]())
    return out


def _fingerprint(r):
    return hashlib.sha256(json.dumps([synth.VERSION, r], sort_keys=True).encode()).hexdigest()[:16]


def paths(r, cache=CACHE):
    stem = cache / r["id"]
    return stem.with_suffix(".f32"), stem.with_suffix(".truth.npz"), stem.with_suffix(".json")


def ensure(r, cache=CACHE):
    """The vector's audio and truth, generated unless the cache holds them for this recipe."""
    audio_path, truth_path, recipe_path = paths(r, cache)
    if recipe_path.exists() and audio_path.exists() and truth_path.exists():
        stored = json.loads(recipe_path.read_text())
        if stored.get("_fingerprint") == _fingerprint(r):
            return np.fromfile(audio_path, dtype=np.float32), synth.Truth.load(truth_path)
    cache.mkdir(parents=True, exist_ok=True)
    audio, truth = synth.render(r)
    audio.tofile(audio_path)
    truth.save(truth_path)
    recipe_path.write_text(json.dumps(dict(r, _fingerprint=_fingerprint(r)), indent=1, sort_keys=True))
    return audio, truth
