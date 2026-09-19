"""The tracker harness, from validation/:

    venv/bin/python -m tracker.run generate [--set grid,random,named]
    venv/bin/python -m tracker.run score --estimator yin [--set ...] [--only substring] [--by axis,...]
    venv/bin/python -m tracker.run show ID

generate renders every vector of the chosen sets into the cache. score runs an
estimator over them, prints each vector's metrics with --verbose, and aggregates
along the axes named by --by (default: every tag). show prints a recipe and a
summary of its truth.
"""
from __future__ import annotations

import argparse
import json
import sys
import time

import numpy as np

from . import corpus, score
from .estimators import get as get_estimator


def cmd_generate(args):
    rs = corpus.recipes(args.set)
    t0 = time.time()
    for i, r in enumerate(rs):
        corpus.ensure(r)
        if (i + 1) % 25 == 0 or i + 1 == len(rs):
            print(f"{i + 1}/{len(rs)} {time.time() - t0:.0f} s", flush=True)


def cmd_score(args):
    rs = [r for r in corpus.recipes(args.set) if not args.only or args.only in r["id"]]
    estimate = get_estimator(args.estimator)
    rows = []
    t0 = time.time()
    for r in rs:
        audio, truth = corpus.ensure(r)
        traj = estimate(audio, r["fs"])
        m = score.score(truth, traj, r["tags"])
        if r["tags"].get("case") == "vibrato":
            gain, lag = score.modulation_response(truth, traj, r["tags"]["rate_hz"])
            m["mod_gain"], m["mod_lag_cycles"] = gain, lag
        rows.append(m)
        if args.verbose:
            print(f"{r['id']:40s} " + " ".join(f"{k}={v:.3f}" if isinstance(v, float) else f"{k}={v}"
                                              for k, v in m.items() if not k.startswith("tag:")))
    print(f"{len(rows)} vectors, {args.estimator}, {time.time() - t0:.0f} s")
    axes = args.by.split(",") if args.by else sorted({k[4:] for r in rows for k in r if k.startswith("tag:")})
    for axis in axes:
        groups = score.aggregate(rows, axis)
        if groups:
            print()
            print(score.table(groups, f"by {axis}"))
    if any(r["tags"].get("case") == "vibrato" for r in rs):
        print()
        print("vibrato response (gain, lag in cycles):")
        for r, m in zip(rs, rows):
            if r["tags"].get("case") == "vibrato":
                print(f"  {r['id']:40s} gain {m['mod_gain']:.2f}  lag {m['mod_lag_cycles']:.2f}")


def cmd_show(args):
    for r in corpus.recipes("all"):
        if r["id"] == args.id:
            audio, truth = corpus.ensure(r)
            print(json.dumps(r, indent=1))
            voiced = truth.voiced
            f0 = truth.f0[voiced]
            print(f"audio: {audio.shape[0]} samples, peak {np.max(np.abs(audio)):.3f}, rms {np.sqrt(np.mean(audio ** 2)):.4f}")
            print(f"truth: voiced {np.mean(voiced) * 100:.0f}%, f0 {f0.min():.2f}..{f0.max():.2f} Hz" if f0.size else "truth: unvoiced")
            print(f"onsets {truth.onsets}, glitches {truth.glitches}, partials {truth.partials[:12]}{'...' if len(truth.partials) > 12 else ''}")
            print(f"notes: {truth.notes}")
            return
    print(f"no recipe {args.id}", file=sys.stderr)
    sys.exit(1)


def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("generate")
    g.add_argument("--set", default="all")
    g.set_defaults(fn=cmd_generate)
    s = sub.add_parser("score")
    s.add_argument("--estimator", default="yin")
    s.add_argument("--set", default="all")
    s.add_argument("--only", default="")
    s.add_argument("--by", default="")
    s.add_argument("--verbose", action="store_true")
    s.set_defaults(fn=cmd_score)
    w = sub.add_parser("show")
    w.add_argument("id")
    w.set_defaults(fn=cmd_show)
    args = p.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
