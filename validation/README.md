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
- `signals.py` — deterministic float32 test signals: sines, band-limited sawtooth and square, twin tones, xorshift noise, silence. The C tests generate the same samples from the same parameters.
- `period.py` — the normalized autocorrelation of a windowed frame from its spectrum, the period as the first peak within a tolerance of the highest with parabolic refinement, and the conditioner that turns per-frame estimates into a fundamental with a validity: gate, clarity hysteresis, hold, octave guard, smoothing, dead band.
- `pll.py` — a phase-locked loop over the times of a periodic event: a clock with a period and a phase that follows measured times by one time constant, critically damped, re-seating on a slip.
- `harmonics.py` — per-harmonic readouts from a spectrum: the peak nearest each harmonic when it stands a prominence above the floor beside it, its level refined by a parabola less the window's bias, its phase relative to the fundamental, and the distortion the harmonics add up to.

## Scripts

```sh
# Regenerate the golden tables the C tests validate against
python validation/scripts/emit_period_golden.py
python validation/scripts/emit_harmonics_golden.py
python validation/scripts/emit_pll_golden.py
```

Each writes a header under `test/golden/`. Regenerate and commit when an algorithm or its cases change.

```sh
# Measure how the readout rules behave on noise, to choose their defaults
python validation/scripts/measure_prominence.py
python validation/scripts/measure_averaging.py
```

The first reads harmonics of pure noise and reports how often noise passes for one, by window, transform size, shoulder length, and threshold. The second slides the real window over a sine with one weak harmonic in noise and reports false readouts, hits, and flips against averaging time, thresholds, and the label gate.
