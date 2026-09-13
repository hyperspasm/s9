#ifndef harmonic_series_h
#define harmonic_series_h

#include "harmonic_readout.h"

#include <stddef.h>
#include <stdint.h>

// The fundamental of the harmonic series in a spectrum. Partials are the
// prominent peaks of the magnitude spectrum in dB: local maxima within
// peak_floor_db of the strongest that stand prominence_db above the floor
// beside them, each refined by a parabola less the window's bias. The
// fundamental is found in two stages. First, the largest frequency of which
// every strong partial, within strong_db of the strongest, is a multiple,
// taking the lowest strong partial as harmonic 1, 2, 3, ... until the rest
// fall on the series; a series above f_max means no fundamental in range,
// one a little below f_min still counts, and when no series holds every
// strong partial, the one holding most of their power is taken among those
// whose partials fill at least a fraction of the slots between the lowest
// and the highest of them. Second, a sub-multiple of that is taken instead
// only when the weaker partials fill more than a fraction of the extra
// slots it predicts between the lowest and highest partial on it, with at
// least min_extra of them carrying more than extra_power of the series'
// power: what a real subharmonic series does and aliases or noise do not.
// The fine value is a least squares of f_k = k f0 over the partials on the
// series, weighted by their power and by how closely they lie on it, so a
// stray peak near a multiple barely counts; partials more than fit_floor_db
// below the strongest stay out of it. With it come the harmonicity, the
// frame's power within the partials on the series over its total, the count
// of those partials, and an uncertainty in cents from their spread. Pure
// functions over caller-owned memory.

typedef struct {
	float f_min, f_max;        // Hz, the fundamentals looked for
	float peak_floor_db;       // peaks this far below the strongest are not partials
	float prominence_db;       // a partial stands this far above the floor beside it
	uint32_t shoulder;         // bins of spectrum on each side, past the lobe, that make the floor
	float strong_db;           // partials within this of the strongest anchor the series
	uint32_t divisors;         // the lowest strong partial is tried as this many harmonics
	float coarse_bins;         // a partial this close to a multiple of a first candidate is on it
	float fine_bins;           // and of a refined one
	float sparse;              // a series taken for holding the most power must fill this fraction of its slots
	float completeness;        // a sub-multiple must fill this fraction of its extra slots
	uint32_t min_extra;        // with at least this many partials
	float extra_power;         // carrying this fraction of the series' power
	float fit_floor_db;        // partials this far below the strongest stay out of the fit
	float stray_bins;          // a partial this far off its multiple weighs half in the fit
} harmonic_series_config_t;

static const harmonic_series_config_t default_harmonic_series_config = {
	.f_min = 20.0f,
	.f_max = 4000.0f,
	.peak_floor_db = -60.0f,
	.prominence_db = 12.0f,
	.shoulder = 8,
	.strong_db = 20.0f,
	.divisors = 32,
	.coarse_bins = 1.5f,
	.fine_bins = 0.5f,
	.sparse = 0.3f,
	.completeness = 0.5f,
	.min_extra = 2,
	.extra_power = 0.05f,
	.fit_floor_db = 40.0f,
	.stray_bins = 0.05f,
};

typedef struct {
	float f0;                  // Hz; 0 when there is no series in range
	float harmonicity;         // the frame's power within the partials on the series, over its total
	uint32_t count;            // partials on the series
	float uncertainty_cents;
} harmonic_series_t;

// The scratch memory an estimate over a spectrum of bins values needs, in
// bytes, aligned as malloc aligns.
size_t harmonic_series_scratch_size(uint32_t bins);

// The estimate from a spectrum in dB, bins values delta_f apart, as
// harmonic_magnitude_db writes it, with the model of the window it was
// taken under.
harmonic_series_t harmonic_series_estimate(const float *db, uint32_t bins, double delta_f, const harmonic_window_t *w,
                                           const harmonic_series_config_t *config, void *scratch);

#endif /* harmonic_series_h */
