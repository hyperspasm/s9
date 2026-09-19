"""What the two-stage tracker saw on a vector, hop by hop: the lock, the truth, the
decision and its evidence.

    venv/bin/python -m tracker.debug ID [--from S] [--to S] [--every N]
"""
from __future__ import annotations

import argparse
import sys

import numpy as np

from . import corpus
from .estimators import two_stage


def main():
    p = argparse.ArgumentParser()
    p.add_argument("id")
    p.add_argument("--from", dest="start", type=float, default=0.0)
    p.add_argument("--to", dest="stop", type=float, default=1e9)
    p.add_argument("--every", type=int, default=5)
    args = p.parse_args()
    r = [x for x in corpus.recipes("all") if x["id"] == args.id]
    if not r:
        sys.exit(f"no recipe {args.id}")
    r = r[0]
    audio, truth = corpus.ensure(r)
    traj = two_stage.estimate(audio, r["fs"])
    print(f"{'t':>6s} {'truth':>8s} {'lock':>8s} {'corr':>5s} {'f0':>8s} {'cmt':>3s} {'dec':>6s} {'eval':>6s} {'harm':>5s} {'ratio':>7s} {'peaks':>5s} {'on':>3s}  sub_seen")
    for i in range(0, len(traj.t), args.every):
        t = traj.t[i]
        if t < args.start or t > args.stop:
            continue
        d = traj.debug[i]
        f_true = float(np.interp(t, truth.t, truth.f0))
        dec = d.get("decision")
        ev = f"({d['g']},{d['m']})" if "g" in d else "-"
        if d.get("building"):
            ev += "?"
        print(f"{t:6.2f} {f_true:8.2f} {traj.lock_hz[i]:8.2f} {traj.lock_conf[i]:5.2f} {traj.f0_hz[i]:8.2f} {int(traj.committed[i]):3d} "
              f"{str(dec) if dec else '-':>6s} {ev:>6s} {d.get('harmonicity', 0.0):5.2f} {d.get('ratio', 0.0):7.4f} {d.get('peaks', 0):5d} {d.get('on', 0):3d}  "
              + " ".join(f"{m}:{v:.2f}" for m, v in sorted(d.get("sub_seen", {}).items())))


if __name__ == "__main__":
    main()
