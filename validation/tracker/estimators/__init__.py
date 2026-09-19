"""Estimators the harness can run: each a function estimate(audio, fs) -> score.Trajectory,
registered by name. An estimator without a lock stage reports its fundamental as the
lock as well.
"""
from __future__ import annotations

from importlib import import_module

NAMES = {
    "yin": "tracker.estimators.yin",
    "current": "tracker.estimators.current",
}


def get(name):
    module = import_module(NAMES[name])
    return module.estimate
