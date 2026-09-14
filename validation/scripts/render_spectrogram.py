#!/usr/bin/env python3
"""Render spectrograms of synthetic signals through the constant-Q readout, as the
display would show them, to PNG files for looking at. Nothing here belongs in the
repository but the script.

    python validation/scripts/render_spectrogram.py OUT_DIR [--fs 48000] [--columns 1200] [--min-db -90] [--min-hops 1]

Each image is one row per hop, a hop being one display frame at 60 Hz, time running
down the image so that the newest row is at the bottom, log frequency across it from
20 Hz to 20 kHz, and level as colour from --min-db up to full scale; the shortest
window is --min-hops hops, so more than 1 overlaps the rows. Ticks below mark
the decades, 100 Hz, 1 kHz and 10 kHz, with a tall line, and the steps between them
with short ones; ticks on the left mark seconds. The signals: an exponential sweep;
a drum pattern at 120 BPM with a hi-hat on every sixteenth, a kick on every beat and a
snare on the second and fourth; a chromatic run of sawtooth notes, one every tenth of
a second; and a vowel with a weak fundamental at 110 Hz, then at 220 Hz.
"""
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
import numpy as np  # noqa: E402

from s9 import constant_q, recipes, signals, window  # noqa: E402
from _png import write_png  # noqa: E402

N, MIN_DB_DEFAULT, F_LO, F_HI, FPS = 32768, -90.0, 20.0, 20000.0, 60
SECONDS = 8.0
LEFT, BOTTOM = 16, 12   # the tick margins, pixels

# level from 0 at the floor to 1 at full scale, to colour
STOPS = [(0.0, (0, 0, 0)), (0.2, (20, 10, 60)), (0.4, (120, 20, 120)), (0.6, (220, 60, 40)), (0.8, (250, 170, 30)), (1.0, (255, 255, 220))]


def colour_table():
    table = np.zeros((256, 3), dtype=np.uint8)
    for i in range(256):
        u = i / 255.0
        for (u0, c0), (u1, c1) in zip(STOPS, STOPS[1:]):
            if u0 <= u <= u1:
                t = (u - u0) / (u1 - u0)
                table[i] = [round(c0[k] + t * (c1[k] - c0[k])) for k in range(3)]
                break
    return table


def sweep(fs, seconds):
    t = np.arange(int(seconds * fs)) / fs
    rate = math.log(F_HI / F_LO) / seconds
    phase = 2.0 * math.pi * F_LO / rate * (np.exp(rate * t) - 1.0)
    return (0.5 * np.sin(phase)).astype(np.float32)


def drums(fs, seconds):
    n = int(seconds * fs)
    x = np.zeros(n)
    rng = np.random.default_rng(3)
    beat = 60.0 / 120.0
    t_hit = np.arange(0, int(0.2 * fs)) / fs
    for k in range(int(seconds / (beat / 4))):
        start = int(k * beat / 4 * fs)
        burst = rng.uniform(-1.0, 1.0, len(t_hit))
        burst = np.diff(burst, prepend=0.0)   # a first difference, most of its power high
        hat = 0.4 * burst * np.exp(-t_hit / 0.02)
        x[start:start + len(hat)] += hat[:n - start]
        if k % 4 == 0:   # the beat
            kick_t = t_hit
            freq = 50.0 + 70.0 * np.exp(-kick_t / 0.03)
            kick = 0.8 * np.sin(2.0 * math.pi * np.cumsum(freq) / fs) * np.exp(-kick_t / 0.15)
            x[start:start + len(kick)] += kick[:n - start]
        if k % 8 == 4:   # the second and fourth beats
            snare = 0.3 * rng.uniform(-1.0, 1.0, len(t_hit)) * np.exp(-t_hit / 0.08)
            snare += 0.3 * np.sin(2.0 * math.pi * 180.0 * t_hit) * np.exp(-t_hit / 0.1)
            x[start:start + len(snare)] += snare[:n - start]
    return np.clip(x, -1.0, 1.0).astype(np.float32)


def chromatic(fs, seconds):
    n = int(seconds * fs)
    x = np.zeros(n)
    note_len = int(0.1 * fs)
    fade = int(0.005 * fs)
    envelope = np.ones(note_len)
    envelope[:fade] = np.linspace(0.0, 1.0, fade)
    envelope[-fade:] = np.linspace(1.0, 0.0, fade)
    for k in range(min(49, n // note_len)):
        f = 65.406 * 2.0 ** (k / 12.0)   # from C2
        note = signals.sawtooth(note_len, fs, f, 0.3, harmonics=16).astype(np.float64) * envelope
        x[k * note_len:(k + 1) * note_len] += note
    return x.astype(np.float32)


def vowel(fs, seconds):
    n = int(seconds * fs)
    half = n // 2
    a = signals.partials(half, fs, recipes.vowel(110.0, fs), 0.2)
    b = signals.partials(n - half, fs, recipes.vowel(220.0, fs), 0.2)
    return np.concatenate([a, b]).astype(np.float32)


def render(x, cfg, freqs, kernels, min_db, hop, table):
    padded = np.concatenate([np.zeros(N // 2, dtype=np.float32), x, np.zeros(N // 2, dtype=np.float32)])
    rows = (len(padded) - N) // hop + 1
    image = np.zeros((rows, len(freqs)), dtype=np.uint8)
    for r in range(rows):
        frame = padded[r * hop:r * hop + N].astype(np.float64)
        levels = constant_q.db(constant_q.readout(np.fft.rfft(frame), kernels))
        u = np.clip((levels - min_db) / (0.0 - min_db), 0.0, 1.0)
        image[r] = (u * 255.0 + 0.5).astype(np.uint8)
    return table[image]


def framed(picture, freqs, fs, hop):
    """The picture with tick margins: decades below, seconds on the left."""
    rows, cols, _ = picture.shape
    out = np.zeros((rows + BOTTOM, cols + LEFT, 3), dtype=np.uint8)
    out[:rows, LEFT:] = picture
    log_lo, log_hi = math.log(freqs[0]), math.log(freqs[-1])
    decade = 10.0
    while decade <= freqs[-1]:
        for step in range(1, 10):
            f = step * decade
            if freqs[0] <= f <= freqs[-1]:
                c = LEFT + int(round((math.log(f) - log_lo) / (log_hi - log_lo) * (cols - 1)))
                if step == 1:
                    out[rows:rows + BOTTOM, max(c - 1, 0):c + 1] = 255
                else:
                    out[rows + BOTTOM // 2:rows + BOTTOM, c] = 140
        decade *= 10.0
    second = 0
    while True:
        r = int(round(second * fs / hop))
        if r >= rows:
            break
        out[r, :LEFT] = 255
        half = int(round((second + 0.5) * fs / hop))
        if half < rows:
            out[half, :LEFT // 2] = 140
        second += 1
    return out


def main():
    args = sys.argv[1:]
    if not args or args[0].startswith("--"):
        print(__doc__)
        return 2
    out_dir = args[0]
    fs = int(args[args.index("--fs") + 1]) if "--fs" in args else 48000
    columns = int(args[args.index("--columns") + 1]) if "--columns" in args else 1200
    min_db = float(args[args.index("--min-db") + 1]) if "--min-db" in args else MIN_DB_DEFAULT
    min_hops = int(args[args.index("--min-hops") + 1]) if "--min-hops" in args else 1
    hop = fs // FPS
    os.makedirs(out_dir, exist_ok=True)

    cfg = constant_q.Config(fs=fs, n=N, min_window=hop * min_hops, resolution=1.0, function=window.BLACKMAN_HARRIS,
                            threshold_db=-100.0)
    freqs = constant_q.frequencies(F_LO, F_HI, columns)
    print(f"building {columns} kernels...", flush=True)
    kernels = constant_q.kernels(cfg, freqs)
    table = colour_table()
    print(f"{columns} columns, {F_LO:g} to {F_HI:g} Hz, {FPS} rows per second, {min_db:g} dB to 0 dB")
    for name, make in (("sweep", sweep), ("drums", drums), ("chromatic", chromatic), ("vowel", vowel)):
        x = make(fs, SECONDS)
        picture = framed(render(x, cfg, freqs, kernels, min_db, hop, table), freqs, fs, hop)
        path = os.path.join(out_dir, name + ".png")
        write_png(path, picture)
        print(f"wrote {path}: {picture.shape[1]} x {picture.shape[0]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
