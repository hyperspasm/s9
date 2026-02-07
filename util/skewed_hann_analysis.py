#!/usr/bin/env python3
"""Plot normalized skewed Hann windows and their spectral contamination."""

import argparse
import numpy as np
import matplotlib.pyplot as plt


def skewed_hann(N, skew=0.0):
    """Generate a skewed Hann window.

    Args:
        N: Window length in samples.
        skew: 0.0 = symmetric Hann, positive values shift peak toward
              the end of the window. Practical range 0.0 to ~3.0.
    """
    t = np.linspace(0, 1, N)

    if skew != 0.0:
        gamma = np.exp(-skew)
        t = t ** gamma

    return 0.5 * (1.0 - np.cos(2.0 * np.pi * t))


def normalize_window(w):
    """Normalize so that sum(w) == len(w), matching the C implementation."""
    return w * (len(w) / w.sum())


def window_spectrum_db(w, nfft):
    """Compute the normalized magnitude spectrum in dB."""
    W = np.fft.rfft(w, n=nfft)
    mag = np.abs(W)
    mag /= mag.max()
    return 20.0 * np.log10(mag + 1e-12)


def main():
    parser = argparse.ArgumentParser(
        description="Plot skewed Hann windows and their spectral contamination."
    )
    parser.add_argument(
        "-n", "--steps", type=int, default=6,
        help="Number of skew steps from 0 to --skew-max (default: 6)"
    )
    parser.add_argument(
        "--skew-max", type=float, default=1.0,
        help="Maximum skew value (default: 1.0)"
    )
    parser.add_argument(
        "--fft-length", type=int, default=32768,
        help="FFT length in samples (default: 32768)"
    )
    parser.add_argument(
        "--sample-rate", type=int, default=44100,
        help="Sample rate in Hz (default: 44100)"
    )
    parser.add_argument(
        "--freq-range", type=float, default=200.0,
        help="Frequency range to display in Hz (default: 200)"
    )
    parser.add_argument(
        "-o", "--output", type=str, default="skewed_hann_analysis.png",
        help="Output filename (default: skewed_hann_analysis.png)"
    )

    args = parser.parse_args()

    N = args.fft_length
    fs = args.sample_rate
    skews = np.linspace(0.0, args.skew_max, args.steps)
    time_ms = np.linspace(0, 1000.0 * N / fs, N)

    # Frequency axis for the spectral plot
    freqs = np.fft.rfftfreq(N, d=1.0 / fs)
    freq_mask = freqs <= args.freq_range
    freqs_show = freqs[freq_mask]

    # Colormap
    cmap = plt.cm.viridis
    colors = [cmap(i / (len(skews) - 1)) for i in range(len(skews))]

    fig, axes = plt.subplots(2, 1, figsize=(10, 8))

    # --- Time domain ---
    ax = axes[0]
    for skew, color in zip(skews, colors):
        w = normalize_window(skewed_hann(N, skew))
        label = f"skew = {skew:.2f}"
        style = "--" if skew == 0.0 else "-"
        ax.plot(time_ms, w, style, color=color, label=label, linewidth=1.5)

    ax.set_xlabel("Time (ms)", fontsize=12)
    ax.set_ylabel("Weight", fontsize=12)
    ax.set_title(
        f"Normalized Skewed Hann Windows (N={N}, fs={fs} Hz)", fontsize=13
    )
    ax.legend(fontsize=10)
    ax.set_xlim(0, time_ms[-1])
    ax.set_ylim(bottom=0)
    ax.grid(True, alpha=0.3)

    # --- Frequency domain ---
    ax = axes[1]
    for skew, color in zip(skews, colors):
        w = normalize_window(skewed_hann(N, skew))
        db = window_spectrum_db(w, N)[freq_mask]
        label = f"skew = {skew:.2f}"
        style = "--" if skew == 0.0 else "-"
        ax.plot(freqs_show, db, style, color=color, label=label, linewidth=1.5)

    ax.set_xlabel("Frequency (Hz)", fontsize=12)
    ax.set_ylabel("Magnitude (dB)", fontsize=12)
    ax.set_title("Spectral Contamination", fontsize=13)
    ax.legend(fontsize=10)
    ax.set_ylim(-120, 5)
    ax.set_xlim(0, args.freq_range)
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig(args.output, dpi=150)
    print(f"Saved to {args.output}")


if __name__ == "__main__":
    main()
