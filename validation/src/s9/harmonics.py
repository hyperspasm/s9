"""Per-harmonic readouts from a spectrum, and the distortion they add up to.

Magnitudes are in dB relative to a full-scale sine: with s9's normalized
window, a sine of amplitude a reads 20 log10(a). For harmonic k of the
reference f0, the loudest bin within `search` bins of k*f0 is a harmonic
when it is a local maximum that stands `prominence` dB above the floor
around it; the floor is the median of a shoulder of bins on each side,
starting past the window's main lobe so the peak's own leakage does not
raise it, and the lower side's median is taken so the skirt of a louder
neighbour does not hide a harmonic. A harmonic's peak is refined by a
parabola through the dB values of its bin and neighbours. A parabola is not
the shape of a window's main lobe, so the refinement is biased by an amount
that depends only on the window and on the offset the parabola reports; the
correction table gives that bias and is subtracted.
"""
import math

import numpy as np

CORRECTION_POINTS = 33   # table entries over the offset range 0..0.5
LOBE_END_DB = -40.0      # the main lobe ends where the window's transform has fallen this far below its peak
LOBE_SCAN_STEP = 0.05    # bins, when looking for that
LOBE_SCAN_MAX = 16.0     # bins, when the transform never falls that far


def spectrum(xw):
    """rfft of a windowed float32 frame, in double."""
    return np.fft.rfft(xw.astype(np.float64))


def magnitude_db(bins, n):
    scale = 2.0 / n
    mag = np.abs(bins) * scale
    with np.errstate(divide="ignore"):
        return 20.0 * np.log10(mag)


def power_db(power, n):
    """The same scale from per-bin power, |X|^2, as an average over time holds it."""
    scale = (2.0 / n) ** 2
    with np.errstate(divide="ignore"):
        return 10.0 * np.log10(power * scale)


def parabola(y0, y1, y2):
    """(offset from the centre sample, peak value) of the parabola through three samples."""
    denom = y0 - 2.0 * y1 + y2
    d = 0.5 * (y0 - y2) / denom if denom != 0.0 else 0.0
    return d, y1 - 0.25 * (y0 - y2) * d


def lobe_db(w, delta):
    """The window's transform magnitude, in dB relative to its peak, delta bins from its centre."""
    n = len(w)
    j = np.arange(n, dtype=np.float64)
    wd = w.astype(np.float64)
    value = abs(np.sum(wd * np.exp(-2j * math.pi * delta * j / n)))
    return 20.0 * math.log10(max(value / np.sum(wd), 1e-300))


def lobe_reach(w):
    """Whole bins from a peak's bin to the first bin outside its main lobe,
    wherever the peak sits within its bin: where the lobe ends, plus the
    half bin the peak may be off, rounded up."""
    x = LOBE_SCAN_STEP
    while x < LOBE_SCAN_MAX and lobe_db(w, x) > LOBE_END_DB:
        x += LOBE_SCAN_STEP
    return int(math.ceil(x + 0.5))


SHOULDER_MIN = 3   # the fewest bins a side cut short by the spectrum's edge may have and still count


def floor_db(db, p, reach, shoulder):
    """The floor around bin p: on each side, the median of the `shoulder`
    bins starting `reach` bins away; the lower side's. A side cut short by
    the spectrum's edge uses the bins it has, down to SHOULDER_MIN, and
    empty bins are left out; a side with fewer is ignored, and no side at
    all gives -inf."""
    sides = []
    for lo, hi in ((p - reach - shoulder + 1, p - reach), (p + reach, p + reach + shoulder - 1)):
        lo, hi = max(lo, 0), min(hi, len(db) - 1)
        if hi - lo + 1 >= SHOULDER_MIN:
            values = db[lo:hi + 1]
            values = values[np.isfinite(values)]
            if len(values):
                sides.append(float(np.median(values)))
    return min(sides) if sides else -math.inf


def correction_table(w, points=CORRECTION_POINTS):
    """Bias of the parabolic refinement on this window, indexed by the offset
    the parabola reports, uniformly over 0..0.5: (db_bias, offset_bias).
    The parabola's dB peak is high by db_bias and its offset is off by
    offset_bias, both away from the true peak."""
    reported, db_bias, offset_bias = [], [], []
    for true in np.linspace(0.0, 0.5, 4 * points):
        y = [lobe_db(w, k - true) for k in (-1, 0, 1)]
        d, peak = parabola(*y)
        reported.append(d)
        db_bias.append(peak)            # the true peak is 0 dB by construction
        offset_bias.append(d - true)
    grid = np.linspace(0.0, 0.5, points)
    return np.interp(grid, reported, db_bias), np.interp(grid, reported, offset_bias)


def corrected(d, peak_db, table):
    """Apply the correction table to a parabola's (offset, peak dB)."""
    db_bias, offset_bias = table
    grid = np.linspace(0.0, 0.5, len(db_bias))
    a = min(abs(d), 0.5)
    db = peak_db - float(np.interp(a, grid, db_bias))
    off = d - math.copysign(float(np.interp(a, grid, offset_bias)), d)
    return off, db


def shoulder_fit(spacing_bins, reach, shoulder):
    """The shoulder that fits between harmonics spacing_bins apart: shoulder,
    or less so that it ends before the neighbouring harmonic's lobe, never
    fewer than one bin."""
    fit = int(spacing_bins / 2) - reach
    return max(1, min(shoulder, fit))


def readouts(bins, n, fs, f0, harmonics, search, table, reach, shoulder, prominence):
    """readouts_db of a spectrum's magnitudes."""
    return readouts_db(magnitude_db(bins, n), n, fs, f0, harmonics, search, table, reach, shoulder, prominence)


def readouts_db(db, n, fs, f0, harmonics, search, table, reach, shoulder, prominence):
    """A list of (found, frequency, db, prominence) for harmonics 1..harmonics
    from per-bin levels. A harmonic is found when the loudest bin within
    `search` bins of it is a local maximum `prominence` dB above the floor;
    the prominence is reported for any local maximum, -inf without one, so
    a caller may apply its own thresholds. The shoulder the floor is taken
    from is shortened so it ends before the neighbouring harmonic's lobe."""
    last = n // 2 - 1          # the highest bin a parabola can be centred on
    delta_f = fs / n
    shoulder = shoulder_fit(f0 / delta_f, reach, shoulder)
    out = []
    for k in range(1, harmonics + 1):
        centre = int(round(k * f0 / delta_f))
        if centre > last or centre < 1:
            out.append((False, 0.0, 0.0, -math.inf))
            continue
        lo = max(1, centre - search)
        hi = min(last, centre + search)
        p = lo + int(np.argmax(db[lo:hi + 1]))
        y0, y1, y2 = float(db[p - 1]), float(db[p]), float(db[p + 1])
        if not (y1 > y0 and y1 >= y2):
            out.append((False, 0.0, 0.0, -math.inf))
            continue
        stands = y1 - floor_db(db, p, reach, shoulder)
        if stands < prominence:
            out.append((False, 0.0, 0.0, stands))
            continue
        if not all(math.isfinite(v) for v in (y0, y1, y2)):
            out.append((True, p * delta_f, y1, stands))
            continue
        d, peak = parabola(y0, y1, y2)
        off, value = corrected(d, peak, table)
        out.append((True, (p + off) * delta_f, value, stands))
    return out


def centre_phase(bins, n, p):
    """The phase at the frame's centre of the component peaking at bin p, in
    radians: the bin's phase, which is at the frame's first sample, advanced
    by the bin's frequency over half the frame. Under a symmetric window the
    window's own transform is real within its main lobe, so the peak's
    nearest bin carries no phase from where the peak sits within it."""
    c = (n - 1) / 2.0
    return math.atan2(bins[p].imag, bins[p].real) + 2.0 * math.pi * p * c / n


def relative_phase(bins, n, p_k, p_1, k):
    """Harmonic k's phase relative to the fundamental's, in degrees in
    (-180, 180]: its phase less k times the fundamental's, which is the
    shape of the waveform whatever its time origin."""
    theta = centre_phase(bins, n, p_k) - k * centre_phase(bins, n, p_1)
    deg = math.degrees(theta) % 360.0
    return deg - 360.0 if deg > 180.0 else deg


def phases(bins, n, fs, reads):
    """The relative phase of each harmonic found, from the readouts' peak
    bins; 0 where a harmonic or the fundamental was not found."""
    delta_f = fs / n
    if not reads or not reads[0][0]:
        return [0.0] * len(reads)
    p_1 = int(round(reads[0][1] / delta_f))
    out = []
    for k, (found, freq, _, _) in enumerate(reads, 1):
        out.append(relative_phase(bins, n, int(round(freq / delta_f)), p_1, k) if found else 0.0)
    return out


def thd(reads):
    """Total harmonic distortion: the RMS of the harmonics found over the
    fundamental's, as a ratio; None when the fundamental was not found."""
    if not reads or not reads[0][0]:
        return None
    a1 = 10.0 ** (reads[0][2] / 20.0)
    power = sum((10.0 ** (db / 20.0)) ** 2 for found, _, db, _ in reads[1:] if found)
    return math.sqrt(power) / a1
