"""Estimators the harness can run: each a function estimate(audio, fs) -> score.Trajectory,
registered by name as module or module:function. An estimator without a lock stage
reports its fundamental as the lock as well.
"""
from __future__ import annotations

from importlib import import_module

NAMES = {
    "yin": "tracker.estimators.yin",
    "current": "tracker.estimators.current",                   # through the Python oracle, slow
    "current_c": "tracker.estimators.current_c",               # the C tracker itself, through build/track_vectors
    "pyin": "tracker.estimators.pyin",                         # librosa
    "crepe": "tracker.estimators.crepe",                       # torchcrepe, the full model: slow on a CPU
    "crepe_tiny": "tracker.estimators.crepe:estimate_tiny",    # the tiny model
    "pesto": "tracker.estimators.pesto_pitch",                 # pesto-pitch
    "two_stage": "tracker.estimators.two_stage",               # the objective's design, prototyped
    "tracker_c": "tracker.estimators.tracker_c",               # the same in C, src/tracker.c, through build/tracker_vectors
}


def get(name):
    module_name, _, function = NAMES[name].partition(":")
    return getattr(import_module(module_name), function or "estimate")
