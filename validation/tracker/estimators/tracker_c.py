"""The two-stage tracker in C, src/tracker.c, through the tracker_vectors tool the
build makes (build/tracker_vectors): audio fed in 10 ms hops, a line per hop. The
same design as the two_stage prototype, to be scored against it.
"""
from __future__ import annotations

import os
import subprocess
import tempfile
from pathlib import Path

import numpy as np

from ..score import Trajectory

TOOL = Path(__file__).resolve().parents[3] / "build" / "tracker_vectors"
HOP_S = 0.01


def estimate(audio, fs):
    if fs != 48000:
        raise ValueError("tracker_vectors runs at 48 kHz")
    if not TOOL.exists():
        raise FileNotFoundError(f"{TOOL}: build s9 first (cmake --build build --target tracker_vectors)")
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
    t, lock_hz, conf, f0, committed = rows[:, 0], rows[:, 1], rows[:, 2], rows[:, 3], rows[:, 4] > 0.5
    fed = np.arange(1, rows.shape[0] + 1) * HOP_S
    locked = lock_hz > 0
    latency = float(np.median(fed[locked] - t[locked])) if np.any(locked) else HOP_S
    return Trajectory(t, lock_hz, conf, f0, committed, latency_s=latency)
