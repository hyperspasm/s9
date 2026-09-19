"""YIN (de Cheveigné and Kawahara, 2002), plain: the cumulative mean normalized
difference function per frame, the first dip under a threshold, refined by a
parabola. No lock stage and no memory, so every frame answers alone: the baseline
that shows what the corpus does to a stateless estimator. Each frame's value is
stamped at the frame's end, when it could first be shown.
"""
from __future__ import annotations

import numpy as np

from ..score import Trajectory

WINDOW_S = 0.05
HOP_S = 0.01
THRESHOLD = 0.15
F_MIN, F_MAX = 30.0, 2200.0


def _difference(frame, tau_max):
    """d(tau) for tau in 0..tau_max over a frame of 2 * tau_max samples, by correlation"""
    n = frame.shape[0]
    w = n - tau_max
    x = frame
    size = 1 << (2 * n).bit_length()
    spec = np.fft.rfft(x, size)
    acf = np.fft.irfft(spec * np.conj(spec), size)[:tau_max + 1]
    energy = np.cumsum(x * x)
    e0 = energy[w - 1]
    e_tau = energy[w - 1 + np.arange(tau_max + 1)] - np.concatenate([[0.0], energy[:tau_max]])
    # acf here is over the whole frame; the windowed form uses the first w samples against their shift
    acf_w = np.array([np.dot(x[:w], x[tau:tau + w]) for tau in range(tau_max + 1)]) if w <= 512 else None
    if acf_w is None:
        # a windowed correlation by FFT: x[:w] against the frame
        spec_w = np.fft.rfft(x[:w], size)
        acf_w = np.fft.irfft(np.conj(spec_w) * spec, size)[:tau_max + 1]
    return e0 + e_tau - 2.0 * acf_w


def estimate(audio, fs):
    tau_max = int(fs / F_MIN)
    tau_min = int(fs / F_MAX)
    frame_len = 2 * tau_max
    hop = int(HOP_S * fs)
    times, f0s, confs = [], [], []
    for start in range(0, audio.shape[0] - frame_len, hop):
        frame = audio[start:start + frame_len].astype(np.float64)
        d = _difference(frame, tau_max)
        cmndf = np.ones_like(d)
        running = np.cumsum(d[1:])
        cmndf[1:] = d[1:] * np.arange(1, tau_max + 1) / np.where(running > 0, running, 1e-12)
        f0, conf = 0.0, 0.0
        tau = tau_min
        while tau < tau_max:
            if cmndf[tau] < THRESHOLD:
                while tau + 1 < tau_max and cmndf[tau + 1] < cmndf[tau]:
                    tau += 1
                break
            tau += 1
        if tau < tau_max and cmndf[tau] < THRESHOLD:
            a, b, c = cmndf[tau - 1], cmndf[tau], cmndf[tau + 1]
            denom = a - 2.0 * b + c
            shift = 0.5 * (a - c) / denom if abs(denom) > 1e-12 else 0.0
            f0 = fs / (tau + shift)
            conf = 1.0 - b
        times.append((start + frame_len) / fs)   # when the frame's value could first be shown
        f0s.append(f0)
        confs.append(conf)
    t = np.array(times)
    f0 = np.array(f0s)
    conf = np.array(confs)
    return Trajectory(t, f0, conf, f0, f0 > 0)
