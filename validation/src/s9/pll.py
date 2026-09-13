"""A phase-locked loop over the times of a periodic event.

The loop is a clock with a period and a phase: every event is expected at
phase + n * period. Each measured time moves the phase a fraction of the
way toward it and corrects the period by a smaller fraction of the same
error, the fractions set by a time constant and the time elapsed since the
last measurement, the period's fraction chosen for a damping ratio of 0.7,
so the clock settles without ringing. The time constant grows with the
lock's age until it reaches the configured one: a fresh lock follows its
first measurements closely and measures the period from them, then smooths
more as it ages. So a period that starts off converges within a few events,
the clock settles on the train's average, and only a fraction of any one
measurement's error is passed on. A measurement too far from its prediction
is a slip: the clock re-seats its phase on it and acquires again from
there, its period as the starting point. Times and the time constant share
the caller's unit.
"""
import math

# a lock's time constant is its age over this, until the configured one is smaller
ACQUISITION = 4.0


class Config:
    def __init__(self, tc, capture):
        self.tc = tc                  # time constant of the settled loop; 0 follows each measurement all the way
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
        self.seated = 0.0

    def _seat(self, t):
        self.phase = t
        self.last = t
        self.seated = t
        self.locked = True

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
            self._seat(t)
            return 0.0
        n = _round((t - self.phase) / self.period)
        predicted = self.phase + n * self.period
        error = t - predicted
        if abs(error) > cfg.capture * self.period:
            self._seat(t)
            return error
        dt = t - self.last
        cycles = max(1, _round(dt / self.period))
        tc = min(cfg.tc, (t - self.seated) / ACQUISITION)
        alpha = 1.0 - math.exp(-dt / tc) if tc > 0.0 else 1.0
        beta = alpha * alpha / (2.0 - alpha)      # a damping ratio of 0.7
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
