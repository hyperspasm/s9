"""CREPE (Kim et al., 2018) through torchcrepe: a convolutional network over 64 ms
of raw audio at 16 kHz, 360 pitch bins 20 cents apart, decoded with Viterbi; the
periodicity output is its confidence. Frames every 10 ms, stamped at their centres,
half a frame of latency. Voiced where the periodicity clears CREPE_VOICED. No lock
stage: the decoded pitch is reported as both. The full model is used; it is slow
on a CPU.
"""
from __future__ import annotations

import numpy as np
import torch
import torchcrepe

from ..score import Trajectory

HOP_S = 0.01
F_MIN, F_MAX = 33.0, 2006.0   # CREPE's bins start at C1, 32.7 Hz; a lower fmin masks every bin
CREPE_VOICED = 0.3
WINDOW_S = 1024 / 16000


def _estimate(audio, fs, model):
    hop = int(HOP_S * fs)
    x = torch.from_numpy(audio.astype(np.float32))[None, :]
    pitch, periodicity = torchcrepe.predict(x, fs, hop_length=hop, fmin=F_MIN, fmax=F_MAX, model=model,
                                            decoder=torchcrepe.decode.viterbi, return_periodicity=True,
                                            batch_size=512, device="cpu")
    pitch, periodicity = pitch[0].numpy(), periodicity[0].numpy()
    t = np.arange(pitch.shape[0]) * hop / fs
    voiced = periodicity >= CREPE_VOICED
    f0 = np.where(voiced, pitch, 0.0)
    return Trajectory(t, f0, periodicity, f0, voiced, latency_s=WINDOW_S / 2)


def estimate(audio, fs):
    return _estimate(audio, fs, "full")


def estimate_tiny(audio, fs):
    """the tiny model: a fortieth of the parameters, about fifteen times faster on a CPU"""
    return _estimate(audio, fs, "tiny")
