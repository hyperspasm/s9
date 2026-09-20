# s9 validation

Development-time numeric reference for s9's DSP. The C code is the product; this package is the oracle: it implements the algorithms independently and emits golden tables that the C unit tests check against. Python defines the truth; the C enforces it.

## Setup

```sh
./validation/make_venv.sh
source validation/venv/bin/activate
```

The virtualenv lives at `validation/venv` and is gitignored. It installs numpy and pytest.

## Package (`src/s9/`)

- `window.py` — s9's window functions (`src/window_function.c`), including the skew warp and the float32 normalization, reproduced to the bit so that goldens computed on windowed signals apply to the C.
- `signals.py` — deterministic float32 test signals: sines, band-limited sawtooth and square, twin tones, xorshift noise, silence, and the sum of a table of partials. The C tests generate the same samples from the same parameters.
- `recipes.py` — partial tables of the shapes a fundamental is hard to find in: a vowel with a weak fundamental and formants, the same with harmonics missing or only the odd ones, a sub-oscillator's real subharmonic series, aliases folded about another rate, mains hum, a stretched string, a detuned unison.
- `pll.py` — a phase-locked loop over the times of a periodic event: a clock with a period and a phase that follows measured times by one time constant, damped to a ratio of 0.7, acquiring quickly while a lock is fresh, re-seating and acquiring again on a slip.
- `harmonics.py` — per-harmonic readouts from a spectrum: the peak nearest each harmonic when it stands a prominence above the floor beside it, its level refined by a parabola less the window's bias, its phase relative to the fundamental, and the distortion the harmonics add up to.
- `constant_q.py` — a constant-Q readout from one long transform: the level at each of a set of frequencies through a window sized for that frequency, every window centred on the middle of the same frame, defined as the windowed transform at exactly that frequency and computed as Brown and Puckette's sparse kernels over the frame's transform.

## Tracker (`tracker/`)

The fundamental tracker's objective, corpus and harness, separate from the oracle because it judges any estimator rather than mirroring one. [tracker/objective.md](tracker/objective.md) states what the tracker promises and how each promise is checked. `synth.py` is the generative engine: a recipe over the objective's axes, pitch trajectory, timbre, envelope, nuisances, realism, becomes audio and its truth. `corpus.py` holds the recipes in three sets, a grid over pitch, timbre and noise, a seeded random sample of every axis, and named cases for particular promises; vectors are generated on demand into `tracker/cache/`, which is not committed. `score.py` computes the metrics against a vector's truth and aggregates them along the axes. `estimators/` adapts estimators to the harness: `yin`, a plain stateless YIN; the outside baselines `pyin`, `pesto`, `crepe` and `crepe_tiny`; `two_stage`, the objective's design prototyped in Python; and `tracker_c`, the tracker s9 runs, src/tracker.c, through `build/tracker_vectors`, which prints the lock and the fundamental per hop and, with `-v`, both stages' workings.

```sh
cd validation
venv/bin/python -m pytest tracker                       # the engine's promises
venv/bin/python -m tracker.run generate                 # every vector into the cache, once
venv/bin/python -m tracker.run score --estimator yin    # metrics per axis; --set, --only, --by, --verbose narrow it
venv/bin/python -m tracker.run compare --estimators tracker_c,pyin --by set   # saved runs side by side
venv/bin/python -m tracker.run show grid-C4-saw-clean   # a recipe and its truth
venv/bin/python -m tracker.debug named-sh --every 10    # what the two-stage prototype saw, hop by hop
venv/bin/python -m tracker.plot two_stage named-sh /tmp/sh.png   # a trajectory against the truth
```

## Scripts

```sh
# Regenerate the golden tables the C tests validate against
python validation/scripts/emit_harmonics_golden.py
python validation/scripts/emit_pll_golden.py
python validation/scripts/emit_constant_q_golden.py
```

Each writes a header under `test/golden/`. Regenerate and commit when an algorithm or its cases change.

```sh
# Measure how the readout rules behave on noise, to choose their defaults
python validation/scripts/measure_prominence.py
python validation/scripts/measure_averaging.py
```

The first reads harmonics of pure noise and reports how often noise passes for one, by window, transform size, shoulder length, and threshold. The second slides the real window over a sine with one weak harmonic in noise and reports false readouts, hits, and flips against averaging time, thresholds, and the label gate.

```sh
# Measure the constant-Q readout as the spectrogram uses it
python validation/scripts/measure_constant_q.py [--quiet]

# Render spectrograms of synthetic signals to PNG files, for looking at
python validation/scripts/render_spectrogram.py OUT_DIR [--fs 48000] [--columns 1200] [--min-db -90] [--min-hops 1]
```

The first promises that a tone reads its level at its nearest column, that what the kernels' truncation leaves out stays below a floor, and that a click is read only by the columns whose windows hold it; it reports where the constant-Q regime begins and ends, the kernels' size, and how deep the dip between two tones a semitone apart is. The second writes images of a sweep, a drum pattern, a chromatic run, and a vowel, one row per display frame; the images stay out of the repository.
