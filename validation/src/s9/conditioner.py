"""The conditioning of per-frame estimates of a fundamental into a clean one.

An estimate is accepted when the frame's RMS clears the gate and the estimate's
harmonicity clears a threshold, one to become valid and a lower one to stay so.
While valid, the frequency follows the accepted estimates through the smoother and
the dead band; while invalid, it holds its last value, so a display keeps a stable
picture. The smoother is a one-pole low-pass with its time constant in seconds, so
it averages a wobble out however fast the frames come; an estimate further than
snap_cents from the tracked value is a new note and replaces it outright rather
than being glided to. An estimate an octave from the tracked value is folded onto
it, an estimator's slip, for up to the hold time; one that keeps insisting is a
new note and is followed. Validity survives frames without an accepted estimate
for the hold time. Time is the audio time between updates, so the frame rate does
not matter.
"""
import math


def cents(freq):
    return 1200.0 * math.log2(freq)


class Config:
    def __init__(self, gate_rms, harmonicity_on, harmonicity_off, hold_s, octave_tolerance_cents,
                 smoothing_s, deadband_cents, snap_cents):
        self.gate_rms = gate_rms                    # frames quieter than this have no fundamental
        self.harmonicity_on = harmonicity_on        # harmonicity needed to become valid
        self.harmonicity_off = harmonicity_off      # harmonicity needed to stay valid
        self.hold_s = hold_s                        # time without an accepted estimate tolerated before validity ends, and an
                                                    # octave disagreement tolerated before it is followed, seconds
        self.octave_tolerance_cents = octave_tolerance_cents  # an estimate this close to double or half is folded
        self.smoothing_s = smoothing_s              # time constant of the tracked value's move toward the estimates,
                                                    # seconds; the smoothing is done in cents
        self.deadband_cents = deadband_cents        # the reported value moves only when the tracked one differs by more
        self.snap_cents = snap_cents                # an estimate further than this from the tracked value replaces it outright


class Conditioner:
    def __init__(self, config):
        self.cfg = config
        self.valid = False
        self.hold_s = 0.0        # time without an accepted estimate
        self.folded_s = 0.0      # time the octave guard has been folding
        self.smoothed = None
        self.reported = None

    def update(self, estimate_hz, harmonicity, rms, dt):
        """One frame: the estimate in Hz (0 for none), its harmonicity, the frame's RMS,
        the audio time since the last frame. Returns (valid, frequency)."""
        cfg = self.cfg
        candidate = None
        if rms >= cfg.gate_rms and estimate_hz > 0.0:
            if (not self.valid and harmonicity >= cfg.harmonicity_on) or (self.valid and harmonicity >= cfg.harmonicity_off):
                candidate = estimate_hz

        if candidate is None:
            if self.valid:
                self.hold_s += dt
                if self.hold_s > cfg.hold_s:
                    self.valid = False
        else:
            c = cents(candidate)
            if self.valid:
                d = c - self.smoothed
                fold = 0.0
                if abs(d - 1200.0) <= cfg.octave_tolerance_cents:
                    fold = -1200.0
                elif abs(d + 1200.0) <= cfg.octave_tolerance_cents:
                    fold = 1200.0
                if fold != 0.0:
                    self.folded_s += dt
                    if self.folded_s <= cfg.hold_s:
                        c += fold
                else:
                    self.folded_s = 0.0
                if abs(c - self.smoothed) > cfg.snap_cents:
                    self.smoothed = c
                else:
                    alpha = 1.0 - math.exp(-dt / cfg.smoothing_s) if cfg.smoothing_s > 0.0 else 1.0
                    self.smoothed += alpha * (c - self.smoothed)
            else:
                self.smoothed = c
                self.reported = c
                self.valid = True
                self.folded_s = 0.0
            self.hold_s = 0.0
            if abs(self.smoothed - self.reported) > cfg.deadband_cents:
                self.reported = self.smoothed

        frequency = 2.0 ** (self.reported / 1200.0) if self.reported is not None else 0.0
        return self.valid, frequency
