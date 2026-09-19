"""The generative engine's promises: a vector's truth describes its audio. Run from
validation/ with venv/bin/python -m pytest tracker."""
import math

import numpy as np

from tracker import corpus, synth


def _recipe(**fields):
    return corpus.recipe("test", {"set": "test"}, **fields)


def _peak_hz(audio, fs, lo, hi):
    """the frequency of the strongest spectral peak between lo and hi, refined by a parabola"""
    n = audio.shape[0]
    w = np.hanning(n)
    spec = np.abs(np.fft.rfft(audio * w))
    freqs = np.fft.rfftfreq(n, 1.0 / fs)
    sel = (freqs >= lo) & (freqs <= hi)
    i = np.argmax(np.where(sel, spec, 0.0))
    a, b, c = np.log(spec[i - 1] + 1e-30), np.log(spec[i] + 1e-30), np.log(spec[i + 1] + 1e-30)
    shift = 0.5 * (a - c) / (a - 2.0 * b + c)
    return (i + shift) * fs / n


def test_static_tone_is_at_its_truth():
    r = _recipe(pitch={"kind": "static", "f0": 220.0}, timbre={"kind": "saw"}, realism={"jitter_cents": 0.0, "random_phase": True})
    audio, truth = synth.render(r)
    fs = r["fs"]
    steady = audio[int(0.5 * fs):int(2.5 * fs)]
    assert abs(synth.cents(_peak_hz(steady, fs, 200.0, 240.0), 220.0)) < 0.5
    assert np.all(truth.f0[truth.voiced] == 220.0)
    assert truth.onsets == [0.1]
    assert truth.partials[0] == (1.0, 0.0)


def test_glide_ends_where_it_says():
    r = _recipe(pitch={"kind": "glide", "f0": 110.0, "f_end": 440.0, "start_s": 1.0, "duration_s": 1.0}, timbre={"kind": "sine"},
                realism={"jitter_cents": 0.0}, seconds=3.0)
    audio, truth = synth.render(r)
    fs = r["fs"]
    assert truth.f0[int(0.5 / synth.TRUTH_STEP_S)] == 110.0
    assert abs(truth.f0[int(2.5 / synth.TRUTH_STEP_S)] - 440.0) < 1e-6
    assert np.all(np.diff(truth.f0[int(1.0 / synth.TRUTH_STEP_S):int(2.0 / synth.TRUTH_STEP_S)]) >= 0.0)
    tail = audio[int(2.2 * fs):int(2.8 * fs)]
    assert abs(synth.cents(_peak_hz(tail, fs, 400.0, 480.0), 440.0)) < 1.0


def test_dropout_silences_and_is_recorded():
    r = _recipe(pitch={"kind": "static", "f0": 220.0}, timbre={"kind": "saw"},
                nuisance=[{"kind": "dropout", "start_s": 1.0, "length_s": 0.05}])
    audio, truth = synth.render(r)
    fs = r["fs"]
    gap = np.std(audio[int(1.005 * fs):int(1.045 * fs)])
    before = np.std(audio[int(0.9 * fs):int(0.99 * fs)])
    assert 20.0 * math.log10(gap / before) < -60.0   # only the realism layer's noise floor remains in the gap
    assert truth.glitches == [(1.0, 0.05)]
    assert np.all(truth.voiced[int(1.0 / synth.TRUTH_STEP_S):int(1.05 / synth.TRUTH_STEP_S)])   # a glitch is not silence


def test_negatives_are_unvoiced():
    for neg in ({"kind": "ringmod"}, {"kind": "fm"}, {"kind": "noise"}, {"kind": "silence"}):
        r = _recipe(negative=neg)
        r.pop("realism")
        audio, truth = synth.render(r)
        assert not np.any(truth.voiced)
        assert np.all(truth.f0 == 0.0)


def test_chorus_truth_is_the_mean_with_the_beat_as_tolerance():
    r = _recipe(pitch={"kind": "static", "f0": 220.0}, timbre={"kind": "saw"}, realism={"jitter_cents": 0.0},
                nuisance=[{"kind": "chorus", "depth_cents": 20.0, "rate_hz": 0.0, "mix_db": 0.0}])
    _, truth = synth.render(r)
    mid = int(1.5 / synth.TRUTH_STEP_S)
    assert abs(synth.cents(truth.f0[mid], 220.0) - 10.0) < 1e-6
    assert abs(truth.tolerance_cents[mid] - 10.0) < 1e-6


def test_sub_oscillator_truth_is_the_sub():
    r = _recipe(pitch={"kind": "static", "f0": 220.0}, timbre={"kind": "sub", "sub_db": -12.0}, realism={"jitter_cents": 0.0})
    audio, truth = synth.render(r)
    fs = r["fs"]
    assert np.all(truth.f0[truth.voiced] == 110.0)
    assert truth.partials[0] == (1.0, -12.0)
    steady = audio[int(0.5 * fs):int(2.5 * fs)]
    assert abs(synth.cents(_peak_hz(steady, fs, 100.0, 120.0), 110.0)) < 1.0


def test_steps_and_gates_are_onsets():
    r = _recipe(pitch={"kind": "steps", "notes": [(220.0, 1.0), (330.0, 1.0), (440.0, 1.0)]}, timbre={"kind": "saw"})
    _, truth = synth.render(r)
    assert truth.onsets == [0.1, 1.0, 2.0]
    r = _recipe(pitch={"kind": "static", "f0": 220.0}, timbre={"kind": "saw"},
                amplitude={"level_db": -6.0, "envelope": "gated", "period_s": 1.0, "duty": 0.5})
    _, truth = synth.render(r)
    assert truth.onsets == [0.1, 1.1, 2.1]
    assert not truth.voiced[int(0.8 / synth.TRUTH_STEP_S)]


def test_level_and_snr_are_as_asked():
    r = _recipe(pitch={"kind": "static", "f0": 220.0}, timbre={"kind": "sine"}, amplitude={"level_db": -20.0, "envelope": "sustain"},
                realism={"jitter_cents": 0.0})
    audio, _ = synth.render(r)
    assert abs(20.0 * math.log10(np.max(np.abs(audio))) + 20.0) < 0.2
    r = _recipe(pitch={"kind": "static", "f0": 220.0}, timbre={"kind": "sine"}, amplitude={"level_db": -20.0, "envelope": "sustain"},
                realism={"jitter_cents": 0.0}, nuisance=[{"kind": "noise", "snr_db": 20.0}])
    noisy, _ = synth.render(r)
    fs = r["fs"]
    sel = slice(int(0.5 * fs), int(2.5 * fs))
    noise = noisy[sel] - audio[sel]
    snr = 20.0 * math.log10(np.std(audio[sel]) / np.std(noise))
    assert abs(snr - 20.0) < 0.5


def test_every_corpus_recipe_renders():
    for r in corpus.recipes("named")[:12] + corpus.recipes("random")[:6]:
        audio, truth = synth.render(r)
        assert audio.shape[0] == int(r["seconds"] * r["fs"])
        assert np.max(np.abs(audio)) <= 1.0
        assert truth.t.shape == truth.f0.shape == truth.voiced.shape
