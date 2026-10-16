"""PESTO (Riou et al., 2023): a small self-supervised network over one frame of a
constant-Q transform, trained with pitch-shift equivariance and no labels. Frames
every 10 ms; its confidence output decides voicing where it clears PESTO_VOICED.
Stamped at frame centres; the latency is taken as half the longest CQT window at
its lowest bin. No lock stage: the pitch is reported as both.
"""
from __future__ import annotations

import numpy as np
import torch
import pesto

from ..score import Trajectory

STEP_MS = 10.0
PESTO_VOICED = 0.5
LATENCY_S = 0.08   # about half the CQT's longest window; see the model's configuration


def estimate(audio, fs):
    x = torch.from_numpy(audio.astype(np.float32))
    timesteps, pitch, confidence, _ = pesto.predict(x, fs, step_size=STEP_MS, convert_to_freq=True)
    t = timesteps.numpy() / 1000.0
    pitch, confidence = pitch.numpy(), confidence.numpy()
    voiced = confidence >= PESTO_VOICED
    f0 = np.where(voiced, pitch, 0.0)
    return Trajectory(t, f0, confidence, f0, voiced, latency_s=LATENCY_S)
