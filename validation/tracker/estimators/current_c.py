"""The tracker s9 runs today, the C code itself, through the track_vectors tool the
build makes (build/track_vectors): audio fed in 10 ms display frames, one update
per frame. It matches the oracle to its goldens and runs about a hundred times
faster. No lock stage: the conditioned fundamental is reported as both. Each
frame's value is stamped at the centre of the transform it came from, with the
half window it takes to become available as the latency.
"""
from __future__ import annotations

import os
import subprocess
import tempfile
from pathlib import Path

import numpy as np

from ..score import Trajectory

TOOL = Path(__file__).resolve().parents[3] / "build" / "track_vectors"


def estimate(audio, fs):
    if fs != 48000:
        raise ValueError("track_vectors runs at 48 kHz")
    if not TOOL.exists():
        raise FileNotFoundError(f"{TOOL}: build s9 first (cmake --build build --target track_vectors)")
    with tempfile.NamedTemporaryFile(suffix=".f32", delete=False) as f:
        audio.astype(np.float32).tofile(f)
        path = f.name
    try:
        out = subprocess.run([str(TOOL), path], capture_output=True, text=True, check=True).stdout
    finally:
        os.unlink(path)
    rows = np.array([[float(v) for v in line.split()] for line in out.splitlines() if line.strip()])
    if rows.size == 0:
        empty = np.zeros(0)
        return Trajectory(empty, empty, empty, empty, np.zeros(0, dtype=bool))
    t, f, valid = rows[:, 0], rows[:, 1], rows[:, 2] > 0.5
    f0 = np.where(valid, f, 0.0)
    latency = -float(t[0]) + 480.0 / fs   # the first line's instant is one hop less half the window before its audio
    return Trajectory(t, f0, np.ones_like(f0), f0, valid, latency_s=latency)
