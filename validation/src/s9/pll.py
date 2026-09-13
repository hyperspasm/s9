"""A phase-locked loop over the times of a periodic event.

The loop is a clock with a period and a phase: every event is expected at
phase + n * period. Each measured time moves the phase a fraction of the
way toward it and corrects the period by a smaller fraction of the same
error, the fractions set by one time constant and the time elapsed since
the last measurement, with the damping critical. So the clock settles on
the train's average, a period that starts off converges, and only a
fraction of any one measurement's error is passed on. A measurement too far
from its prediction is a slip, and the clock re-seats its phase on it,
keeping its period. Times and the time constant share the caller's unit.
"""
import math


class Config:
    def __init__(self, tc, capture):
        self.tc = tc                  # time constant of the loop; 0 follows each measurement all the way
        self.capture = capture        # a measurement further than this fraction of the period from its prediction is a slip


def _round(x):
    """Round half away from zero, as C's lround does."""
    return math.floor(x + 0.5) if x >= 0.0 else -math.floor(-x + 0.5)


class Pll:
    def __init__(self, config):
        self.cfg = config
        self.reset()

    def reset(self):
        """Forget the phase and the period."""
        self.locked = False
        self.period = 0.0
        self.phase = 0.0
        self.last = 0.0

    def seed(self, period):
        """The period to start from; the next observation seats the phase."""
        self.period = period
        self.locked = False

    def observe(self, t):
        """One measured event time. Returns its error against the
        prediction, 0 when it seats the phase."""
        cfg = self.cfg
        if self.period <= 0.0:
            return 0.0
        if not self.locked:
            self.phase = t
            self.last = t
            self.locked = True
            return 0.0
        n = _round((t - self.phase) / self.period)
        predicted = self.phase + n * self.period
        error = t - predicted
        if abs(error) > cfg.capture * self.period:
            self.phase = t
            self.last = t
            return error
        dt = t - self.last
        cycles = max(1, _round(dt / self.period))
        alpha = 1.0 - math.exp(-dt / cfg.tc) if cfg.tc > 0.0 else 1.0
        beta = alpha * alpha / (2.0 - alpha)      # critical damping
        self.phase = predicted + alpha * error
        self.period += beta * error / cycles
        self.last = t
        return error

    def next(self, after):
        """The first predicted event at or after a time; None while unlocked."""
        if not self.locked:
            return None
        n = math.ceil((after - self.phase) / self.period)
        return self.phase + n * self.period
