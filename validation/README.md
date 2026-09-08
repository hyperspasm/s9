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
- `pitch.py` — the normalized square difference function from a windowed frame's spectrum, McLeod's peak picking with parabolic refinement, and the conditioner that turns per-frame estimates into a clean fundamental with a validity.
- `harmonics.py` — per-harmonic readouts from a spectrum: local peak search and parabolic interpolation on the log magnitude.

## Scripts

```sh
# Regenerate the golden tables the C tests validate against
python validation/scripts/emit_pitch_golden.py
python validation/scripts/emit_harmonics_golden.py
```

Each writes a header under `test/golden/`. Regenerate and commit when an algorithm or its cases change.
