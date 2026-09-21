"""pYIN (Mauch and Dixon, 2014) as librosa implements it: YIN's candidates with
probabilities, decoded through a hidden Markov model over frames, so it has memory
of a kind. Frames every 10 ms, 85 ms long. Values are stamped at frame centres; the
latency is half a frame. No lock stage: the decoded pitch is reported as both.
"""
from __future__ import annotations

import numpy as np
import librosa

from ..score import Trajectory

FRAME = 4096
HOP_S = 0.01
F_MIN, F_MAX = 30.0, 2200.0


def estimate(audio, fs):
    hop = int(HOP_S * fs)
    f0, voiced, prob = librosa.pyin(audio.astype(np.float32), fmin=F_MIN, fmax=F_MAX, sr=fs,
                                    frame_length=FRAME, hop_length=hop, fill_na=0.0)
    t = np.arange(f0.shape[0]) * hop / fs   # centred frames
    f0 = np.where(voiced, f0, 0.0)
    return Trajectory(t, f0, prob, f0, voiced, latency_s=FRAME / 2 / fs)
