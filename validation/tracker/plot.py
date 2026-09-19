"""A picture of an estimator's trajectory against a vector's truth, as a PNG, with
nothing but numpy: pitch in cents about the truth's median, the truth in grey, the
lock in dim blue, the fundamental in bright where committed and dim where not,
over time; below it the lock's confidence.

    venv/bin/python -m tracker.plot ESTIMATOR ID OUT.png [--span CENTS]
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from _png import write_png  # noqa: E402

from . import corpus  # noqa: E402
from .estimators import get as get_estimator  # noqa: E402

W, H, CONF_H, MARGIN = 1200, 400, 60, 8


def cents(f, ref):
    with np.errstate(divide="ignore", invalid="ignore"):
        return np.where(f > 0, 1200.0 * np.log2(np.where(f > 0, f, 1.0) / ref), np.nan)


def _line(img, xs, ys, rgb, y0, h):
    for x, y in zip(xs, ys):
        if np.isnan(y) or not (0 <= x < W):
            continue
        py = int(y0 + h - 1 - y)
        if 0 <= py - y0 < h:
            img[py, int(x)] = rgb
            if py + 1 - y0 < h:
                img[py + 1, int(x)] = rgb


def render(truth, traj, seconds, span):
    img = np.zeros((H + CONF_H + 3 * MARGIN, W, 3), dtype=np.uint8)
    ref = float(np.median(truth.f0[truth.voiced])) if np.any(truth.voiced) else 100.0
    lo, hi = -span, span
    y_of = lambda c: (np.clip(c, lo, hi) - lo) / (hi - lo) * (H - 1)
    # grid: every 100 cents, the octaves brighter
    for c in range(int(lo // 100) * 100, int(hi) + 1, 100):
        y = int(MARGIN + H - 1 - y_of(c))
        if 0 <= y - MARGIN < H:
            img[y, :, :] = 70 if c % 1200 else 120
    for s in range(0, int(seconds) + 1):
        x = int(s / seconds * (W - 1))
        img[MARGIN:MARGIN + H, x, :] = 70
    tx = truth.t / seconds * (W - 1)
    _line(img, tx, y_of(cents(truth.f0, ref)), (160, 160, 160), MARGIN, H)
    x = traj.t / seconds * (W - 1)
    _line(img, x, y_of(cents(traj.lock_hz, ref)), (60, 90, 200), MARGIN, H)
    f0 = traj.f0_hz.copy()
    _line(img, x[~traj.committed], y_of(cents(f0[~traj.committed], ref)), (140, 90, 40), MARGIN, H)
    _line(img, x[traj.committed], y_of(cents(f0[traj.committed], ref)), (255, 190, 60), MARGIN, H)
    # confidence strip
    y0 = 2 * MARGIN + H
    img[y0:y0 + CONF_H, :, :] = 25
    _line(img, x, np.clip(traj.lock_conf, 0, 1) * (CONF_H - 1), (60, 200, 120), y0, CONF_H)
    # glitches and onsets
    for start, length in truth.glitches:
        x0, x1 = int(start / seconds * (W - 1)), int((start + length) / seconds * (W - 1))
        img[MARGIN - 4:MARGIN - 1, x0:x1 + 1, :] = (200, 60, 60)
    for onset in truth.onsets:
        x0 = int(onset / seconds * (W - 1))
        img[MARGIN - 4:MARGIN - 1, x0:x0 + 2, :] = (60, 200, 60)
    return img


def main():
    p = argparse.ArgumentParser()
    p.add_argument("estimator")
    p.add_argument("id")
    p.add_argument("out")
    p.add_argument("--span", type=float, default=1500.0)
    args = p.parse_args()
    r = [x for x in corpus.recipes("all") if x["id"] == args.id]
    if not r:
        sys.exit(f"no recipe {args.id}")
    r = r[0]
    audio, truth = corpus.ensure(r)
    traj = get_estimator(args.estimator)(audio, r["fs"])
    write_png(args.out, render(truth, traj, r["seconds"], args.span))
    print(args.out)


if __name__ == "__main__":
    main()
