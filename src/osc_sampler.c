#include "osc_sampler.h"
#include "s9.h"
#include "pll.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL_atomic.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// number of sub-sample interpolation steps for fine trigger detection
#define TRIG_FINE_N 32

// auto trigger timeout in seconds - forces a sweep if no trigger is found within this time
#define AUTO_TRIGGER_TIMEOUT 0.02f

// the phase mode's clock: a crossing this far, as a fraction of the period, from where the
// clock expected it is a slip the clock re-seats on
#define LOCK_CAPTURE 0.25
// and a locked frequency changed by more than this fraction is a new note the clock starts over for
#define LOCK_RESEED 0.05

typedef enum {
	trig_state_wait,        // holdoff countdown
	trig_state_pre_trigger, // waiting for signal to arm (opposite side of threshold)
	trig_state_trigger,     // waiting for threshold crossing
	trig_state_sweep,       // outputting sweep data
} trig_state_t;

static const osc_sampler_settings_t default_settings = {
	.trigger_mode = osc_trig_mode_auto,
	.trig_slope = 1,
	.trig_thresh = 0.1f,
	.trig_holdoff = 0.00001f,  // 10µs
	.trig_frequency = 0.0f,
	.trig_cycles = 2,
	.trig_lock_tc = 0.0f,
	.channel = UINT32_MAX, // sum all channels
};

struct osc_sampler {
	uint32_t sample_rate;
	osc_sampler_settings_t settings;

	// input buffer (circular, lock-free SPSC)
	// write_ix == read_ix means empty, (write_ix+1) % size == read_ix means full
	// one slot is always kept empty to distinguish full from empty
	// usable capacity is input_buf_size - 1
	float *input_buf;
	uint32_t input_buf_size;  // always a power of two
	uint32_t buf_mask;        // input_buf_size - 1, for fast modular indexing
	SDL_atomic_t write_ix;    // next write position (advanced by audio thread)
	SDL_atomic_t read_ix;     // next read position (advanced by render thread)

	// resampler configuration
	uint32_t N;         // convolution length (filter taps)
	uint32_t k;         // sinc table oversampling factor

	// resampler pre-computed tables
	uint32_t n_sinc;    // sinc table size (N * k + 1)
	uint32_t center;    // center index of sinc table
	float *sinc;        // oversampled sinc table
	float *sinc_delta;  // for linear interpolation
	float *window;      // window weights per tap

	// resampler state
	float position;     // fractional position (0.0 - 0.999...)
	float ratio;        // cached ratio
	float cutoff;       // anti-alias cutoff (0 < cutoff <= 1)

	// trigger state
	trig_state_t trig_state;
	float prev_sample;               // last sample value seen at scan position
	uint32_t holdoff_remaining;      // holdoff countdown in samples
	uint32_t auto_trigger_remaining; // auto trigger countdown in samples

	// the phase mode's clock, in samples since creation
	uint64_t consumed;               // samples the render thread has read past: read_ix in absolute terms
	pll_t *lock;
	double last_trigger;             // where the last sweep started
};

// Blackman window - excellent sidelobe suppression (~58 dB)
static float blackman(uint32_t n, uint32_t N) {
	return 0.42f
	     - 0.5f  * cosf(2.0f * (float)M_PI * n / (N - 1))
	     + 0.08f * cosf(4.0f * (float)M_PI * n / (N - 1));
}

static void init_sinc_table(osc_sampler_t *s) {
	uint32_t n_sinc = s->n_sinc;

	// build oversampled sinc table
	for (uint32_t i = 0; i < n_sinc; i++) {
		float t = (float)i / (float)s->k - (float)((s->N >> 1) + 1);

		if (t == 0.0f) {
			s->sinc[i] = 1.0f;
		} else {
			s->sinc[i] = sinf(t * (float)M_PI) / (t * (float)M_PI);
		}
	}

	// pre-compute forward sinc deltas for linear interpolation
	for (uint32_t i = 0; i < n_sinc - 1; i++) {
		s->sinc_delta[i] = s->sinc[i + 1] - s->sinc[i];
	}
	s->sinc_delta[n_sinc - 1] = 0.0f;

	// build per-tap window weights with Blackman window
	for (uint32_t j = 0; j < s->N; j++) {
		s->window[j] = blackman(j, s->N);
	}

	// normalize window weights for unity DC gain at fc=1.0, offset=0
	float sum = 0.0f;
	uint32_t idx = s->k;
	for (uint32_t j = 0; j < s->N; j++, idx += s->k) {
		sum += s->sinc[idx] * s->window[j];
	}
	if (sum != 0.0f) {
		float scale = 1.0f / sum;
		for (uint32_t j = 0; j < s->N; j++) {
			s->window[j] *= scale;
		}
	}
}

// lookup sinc value at fractional table index with linear interpolation
static S9_FORCE_INLINE float sinc_lookup(const osc_sampler_t *s, float x) {
	uint32_t n_sinc = s->n_sinc;

	// clamp x to valid range
	if (x < 0.0f) x = 0.0f;
	if (x > (float)(n_sinc - 1)) x = (float)(n_sinc - 1);

	// clamp integer index so i+1 is safe
	uint32_t i = (uint32_t)x;
	if (i >= n_sinc - 1) i = n_sinc - 2;

	float frac = x - (float)i;

	// linear interpolation using pre-computed deltas
	return s->sinc[i] + frac * s->sinc_delta[i];
}

// compute interpolated output sample at fractional offset with anti-aliasing
// base_ix is the starting index in input_buf for the N-sample convolution window
static float sinc_interpolate(const osc_sampler_t *s, uint32_t base_ix, float offset) {
	float fc = s->cutoff;
	float center_f = (float)s->center;

	// scale offset by oversampling factor
	float off_k = offset * (float)s->k;

	// compute fractional part for phase interpolation
	float kf = off_k - floorf(off_k);

	// compute initial kernel integer index
	uint32_t ki = s->k - (uint32_t)(off_k - kf);

	// perform convolution over input buffer with anti-aliasing
	float accum = 0.0f;
	float coeff_sum = 0.0f;
	uint32_t idx = ki;

	for (uint32_t tap = 0; tap < s->N; tap++, idx += s->k) {
		uint32_t buf_ix = (base_ix + tap) & s->buf_mask;

		float base = (float)idx - kf;
		float scaled = center_f + (base - center_f) * fc;
		float sinc_val = sinc_lookup(s, scaled);
		float coeff = fc * sinc_val * s->window[tap];

		accum += s->input_buf[buf_ix] * coeff;
		coeff_sum += coeff;
	}

	// normalize for unity gain
	if (coeff_sum != 0.0f) {
		accum /= coeff_sum;
	}

	return accum;
}

// get number of samples available for reading given snapshotted indices
static S9_FORCE_INLINE uint32_t samples_available(uint32_t buf_mask, uint32_t write_ix, uint32_t read_ix) {
	return (write_ix - read_ix) & buf_mask;
}

// advance auto trigger countdown by one sample
// called once per sample consumed in pre_trigger/trigger states
// returns 1 if auto trigger fired (state set to sweep), 0 otherwise
static S9_FORCE_INLINE int auto_trigger_tick(osc_sampler_t *s) {
	if (s->settings.trigger_mode != osc_trig_mode_auto) return 0;
	if (s->auto_trigger_remaining == 0) return 0;
	s->auto_trigger_remaining--;
	if (s->auto_trigger_remaining > 0) return 0;

	// timeout expired: force sweep at current position
	s->position = 0.0f;
	s->ratio = 0.0f;
	s->trig_state = trig_state_sweep;
	return 1;
}

// whether the phase mode is in force: it needs a frequency to lock to
static S9_FORCE_INLINE int phase_mode(const osc_sampler_t *s) {
	return s->settings.trigger_mode == osc_trig_mode_phase && s->settings.trig_frequency > 0.0f;
}

// The phase of the component at the trigger frequency at buffer index at,
// measured over the length samples from there on: a rectangular window's
// transform at that one frequency. Over whole cycles the window is
// orthogonal to the component's harmonics, so they do not move the phase;
// the caller makes length whole cycles. For x = A cos(w n + phi) the sum
// of x e^(-i w n) is (A length / 2) e^(i phi), so its argument is phi.
static double phase_at(const osc_sampler_t *s, uint32_t at, uint32_t length) {
	double w = 2.0 * M_PI * (double)s->settings.trig_frequency / (double)s->sample_rate;
	double c = cos(w), sn = sin(w);
	double cr = 1.0, ci = 0.0, re = 0.0, im = 0.0;
	for (uint32_t n = 0; n < length; n++) {
		if ((n & 255) == 0) {
			// restart the rotation from an exact value so its rounding does not accumulate
			cr = cos(w * n);
			ci = sin(w * n);
		}
		double x = s->input_buf[(at + n) & s->buf_mask];
		re += x * cr;
		im -= x * ci;
		double next = cr * c - ci * sn;
		ci = cr * sn + ci * c;
		cr = next;
	}
	return atan2(im, re);
}

// The phase mode's measurement: from the scan position, the next zero
// crossing of the locked component in the slope's direction, as a
// fractional number of samples ahead. Returns -1 when too few samples are
// there to measure the phase.
static int phase_crossing(const osc_sampler_t *s, uint32_t read_ix_local, uint32_t avail, double *ahead) {
	double samples_per_cycle = (double)s->sample_rate / (double)s->settings.trig_frequency;
	uint32_t cycles = s->settings.trig_cycles ? s->settings.trig_cycles : 1;
	uint32_t length = (uint32_t)lround(cycles * samples_per_cycle);
	uint32_t room = s->buf_mask - s->N - 2;   // the most the buffer can hold besides the resampler's window
	if (length > room) length = room;
	if (length < 2) length = 2;
	if (avail < length + s->N + 1) return -1;

	uint32_t half_N = s->N / 2;
	double phi = phase_at(s, read_ix_local + half_N, length);
	double target = s->settings.trig_slope > 0 ? -M_PI / 2.0 : M_PI / 2.0;   // cos(theta) crosses zero rising at -pi/2
	double turn = fmod(target - phi, 2.0 * M_PI);
	if (turn < 0.0) turn += 2.0 * M_PI;
	*ahead = turn / (2.0 * M_PI) * samples_per_cycle;
	return 0;
}

// scan input samples through the trigger state machine
// consumes samples until we transition to trig_state_sweep or run out of data
// on transition to sweep: read_ix and position are set for the resampler
static void trigger_scan(osc_sampler_t *s, uint32_t write_ix_snap) {
	if (s->trig_state == trig_state_sweep) return;

	uint32_t read_ix_local = (uint32_t)SDL_AtomicGet(&s->read_ix);
	uint32_t read_ix_start = read_ix_local;
	float threshold = s->settings.trig_thresh;
	int slope = s->settings.trig_slope;
	uint32_t half_N = s->N / 2;

	while (1) {
		uint32_t avail = samples_available(s->buf_mask, write_ix_snap, read_ix_local);

		switch (s->trig_state) {

		case trig_state_sweep:
			goto done;

		case trig_state_wait:
			if (avail == 0) goto done;
			{
				// consume samples during holdoff, up to available
				uint32_t consume = avail;
				if (consume > s->holdoff_remaining)
					consume = s->holdoff_remaining;
				s->holdoff_remaining -= consume;
				read_ix_local = (read_ix_local + consume) & s->buf_mask;
				if (s->holdoff_remaining == 0) {
					s->trig_state = trig_state_pre_trigger;
					s->auto_trigger_remaining = (uint32_t)(AUTO_TRIGGER_TIMEOUT * s->sample_rate);
				}
			}
			break;

		case trig_state_pre_trigger:
			if (phase_mode(s)) {
				// nothing to arm: the locked component's phase says where the crossing is
				s->trig_state = trig_state_trigger;
				break;
			}
			// need N+1 samples: N for potential resampler window + 1 for scan
			if (avail < s->N + 1) goto done;
			{
				// scan at resampler center position
				uint32_t scan_pos = (read_ix_local + half_N) & s->buf_mask;
				float sample = s->input_buf[scan_pos];

				// rising: arm when signal goes below threshold
				// falling: arm when signal goes above threshold
				if (slope > 0 ? (sample < threshold) : (sample > threshold)) {
					s->prev_sample = sample;
					s->trig_state = trig_state_trigger;
				}
				read_ix_local = (read_ix_local + 1) & s->buf_mask;
				if (auto_trigger_tick(s)) goto done;
			}
			break;

		case trig_state_trigger:
			if (phase_mode(s)) {
				double ahead;
				if (phase_crossing(s, read_ix_local, avail, &ahead) != 0) goto done;
				// the crossing measured is one more observation for the clock; the
				// sweep starts where the clock says the next one is
				double scan = (double)(s->consumed + ((read_ix_local - read_ix_start) & s->buf_mask) + half_N);
				pll_observe(s->lock, scan + ahead);
				double start = pll_next(s->lock, scan);
				ahead = start - scan;
				// whole samples ahead move the resampler's window, the fraction is its offset
				uint32_t whole = (uint32_t)ahead;
				read_ix_local = (read_ix_local + whole) & s->buf_mask;
				s->position = (float)(ahead - whole);
				s->ratio = 0.0f;  // force resample_read to recalculate cutoff
				s->last_trigger = start;
				s->trig_state = trig_state_sweep;
				goto done;
			}
			// need N+1 samples: N for resampler window + 1 for crossing check
			if (avail < s->N + 1) goto done;
			{
				uint32_t scan_pos = (read_ix_local + half_N) & s->buf_mask;
				float curr = s->input_buf[scan_pos];

				// check for threshold crossing
				int crossing = slope > 0
					? (s->prev_sample < threshold && curr >= threshold)
					: (s->prev_sample > threshold && curr <= threshold);

				if (crossing) {
					// coarse crossing found between prev scan_pos and scan_pos
					// read_ix_local is already at scan_pos - N/2 (resampler base)
					// but the crossing is between scan_pos-1 and scan_pos,
					// so step read_ix back by 1 to center on the crossing
					read_ix_local = (read_ix_local - 1) & s->buf_mask;

					// fine trigger detection using sinc interpolation
					// evaluate TRIG_FINE_N points between the two raw samples
					s->cutoff = 1.0f;  // full bandwidth for trigger detection
					float prev_val = s->prev_sample;
					float trigger_pos = 0.0f;  // fallback: start of interval
					int found = 0;

					for (uint32_t j = 1; j <= TRIG_FINE_N; j++) {
						float frac = (float)j / (float)TRIG_FINE_N;
						float val = sinc_interpolate(s, read_ix_local, frac);

						int fine_crossing = slope > 0
							? (prev_val < threshold && val >= threshold)
							: (prev_val > threshold && val <= threshold);

						if (fine_crossing) {
							// linear interpolate between these two fine points
							float sub_frac = (threshold - prev_val) / (val - prev_val);
							trigger_pos = ((float)(j - 1) + sub_frac) / (float)TRIG_FINE_N;
							found = 1;
							break;
						}
						prev_val = val;
					}

					if (!found) {
						// sinc didn't confirm crossing - use linear on raw samples
						float denom = curr - s->prev_sample;
						if (denom != 0.0f)
							trigger_pos = (threshold - s->prev_sample) / denom;
					}

					s->position = trigger_pos;
					s->ratio = 0.0f;  // force resample_read to recalculate cutoff
					s->last_trigger = (double)(s->consumed + ((read_ix_local - read_ix_start) & s->buf_mask) + half_N) + trigger_pos;
					s->trig_state = trig_state_sweep;
					goto done;
				}

				s->prev_sample = curr;
				read_ix_local = (read_ix_local + 1) & s->buf_mask;
				if (auto_trigger_tick(s)) goto done;
			}
			break;
		}
	}

done:
	s->consumed += (read_ix_local - read_ix_start) & s->buf_mask;
	SDL_AtomicSet(&s->read_ix, (int)read_ix_local);
}

// resample from input buffer to output buffer, applying gain
// returns number of output samples written
static uint32_t resample_read(osc_sampler_t *s, float *out, uint32_t count, float ratio, float gain) {
	if (ratio <= 0.0f) return 0;

	// update ratio and cutoff if changed
	if (s->ratio != ratio) {
		s->ratio = ratio;
		// anti-alias cutoff: when downsampling (ratio < 1), reduce bandwidth
		s->cutoff = (ratio < 1.0f) ? ratio : 1.0f;
	}

	// increment is how much we advance through input per output sample
	float increment = 1.0f / ratio;

	// minimum samples needed: N for convolution
	uint32_t min_samples = s->N;

	// snapshot write_ix once (written by audio thread)
	uint32_t write_ix_snap = (uint32_t)SDL_AtomicGet(&s->write_ix);

	// work with local read_ix (only render thread writes this)
	uint32_t read_ix_local = (uint32_t)SDL_AtomicGet(&s->read_ix);
	uint32_t read_ix_start = read_ix_local;

	uint32_t written = 0;

	while (written < count) {
		// check if we have enough samples for convolution + position advance
		uint32_t advance = (uint32_t)(s->position + increment);
		uint32_t avail = samples_available(s->buf_mask, write_ix_snap, read_ix_local);
		if (avail < min_samples + advance) {
			break;  // not enough data
		}

		// compute interpolated output sample
		out[written++] = sinc_interpolate(s, read_ix_local, s->position) * gain;

		// advance position
		s->position += increment;

		// consume input samples as needed
		while (s->position >= 1.0f) {
			s->position -= 1.0f;
			read_ix_local = (read_ix_local + 1) & s->buf_mask;
		}
	}

	// publish final read_ix with release semantics
	s->consumed += (read_ix_local - read_ix_start) & s->buf_mask;
	SDL_AtomicSet(&s->read_ix, (int)read_ix_local);

	return written;
}

osc_sampler_t *osc_sampler_new(uint32_t sample_rate, uint32_t buffer_size) {
	osc_sampler_t *sampler = malloc(sizeof(osc_sampler_t));
	CHECK_ALLOC(sampler, return NULL);
	memset(sampler, 0, sizeof(*sampler));

	sampler->sample_rate = sample_rate;
	sampler->settings = default_settings;

	sampler->lock = malloc(pll_alloc_size());
	CHECK_ALLOC(sampler->lock, osc_sampler_free(&sampler); return NULL);
	pll_init(sampler->lock, &(pll_config_t){ .tc = 0.0, .capture = LOCK_CAPTURE });

	// resampler configuration (hardcoded for now)
	sampler->N = 17;
	sampler->k = 64;
	sampler->n_sinc = sampler->N * sampler->k + 1;
	sampler->center = ((sampler->N >> 1) + 1) * sampler->k;

	// allocate resampler tables
	sampler->sinc = malloc(sampler->n_sinc * sizeof(float));
	CHECK_ALLOC(sampler->sinc, osc_sampler_free(&sampler); return NULL);

	sampler->sinc_delta = malloc(sampler->n_sinc * sizeof(float));
	CHECK_ALLOC(sampler->sinc_delta, osc_sampler_free(&sampler); return NULL);

	sampler->window = malloc(sampler->N * sizeof(float));
	CHECK_ALLOC(sampler->window, osc_sampler_free(&sampler); return NULL);

	init_sinc_table(sampler);

	// initialize resampler state
	sampler->position = 0.0f;
	sampler->ratio = 0.0f;
	sampler->cutoff = 1.0f;

	// input buffer needs at least N samples for convolution + 1 for empty slot
	uint32_t min_buffer_size = sampler->N + 1;
	CHECK_ERROR(buffer_size >= min_buffer_size,
		osc_sampler_free(&sampler); return NULL,
		"buffer_size %u too small, need at least %u", buffer_size, min_buffer_size);

	// round up to next power of two for fast modular indexing
	uint32_t pow2 = 1;
	while (pow2 < buffer_size) pow2 <<= 1;

	sampler->input_buf = malloc(pow2 * sizeof(float));
	CHECK_ALLOC(sampler->input_buf, osc_sampler_free(&sampler); return NULL);
	memset(sampler->input_buf, 0, pow2 * sizeof(float));
	sampler->input_buf_size = pow2;
	sampler->buf_mask = pow2 - 1;

	// initialize atomic indices
	SDL_AtomicSet(&sampler->write_ix, 0);
	SDL_AtomicSet(&sampler->read_ix, 0);

	// initialize trigger state
	sampler->trig_state = trig_state_pre_trigger;
	sampler->prev_sample = 0.0f;
	sampler->holdoff_remaining = 0;
	sampler->auto_trigger_remaining = (uint32_t)(AUTO_TRIGGER_TIMEOUT * sampler->sample_rate);

	return sampler;
}

void osc_sampler_free(osc_sampler_t **sampler) {
	if (sampler && *sampler) {
		if ((*sampler)->sinc) free((*sampler)->sinc);
		if ((*sampler)->sinc_delta) free((*sampler)->sinc_delta);
		if ((*sampler)->window) free((*sampler)->window);
		if ((*sampler)->input_buf) free((*sampler)->input_buf);
		free((*sampler)->lock);
		free(*sampler);
		*sampler = NULL;
	}
}

void osc_sampler_set_settings(osc_sampler_t *sampler, const osc_sampler_settings_t *settings) {
	if (!sampler || !settings) return;
	sampler->settings = *settings;

	// the phase mode's clock: its time constant in samples, and its period seeded from the
	// frequency locked to when it has none or the frequency has moved to a new note
	pll_set_config(sampler->lock, &(pll_config_t){ .tc = (double)settings->trig_lock_tc * sampler->sample_rate, .capture = LOCK_CAPTURE });
	if (settings->trig_frequency > 0.0f) {
		double nominal = (double)sampler->sample_rate / (double)settings->trig_frequency;
		if (!pll_locked(sampler->lock) || fabs(nominal - pll_period(sampler->lock)) > LOCK_RESEED * pll_period(sampler->lock))
			pll_seed(sampler->lock, nominal);
	}
}

uint32_t osc_sampler_enqueue(osc_sampler_t *sampler, const float *samples, uint32_t frames, uint32_t n_channels) {
	if (!sampler || !samples || frames == 0) return 0;

	// snapshot read_ix (written by render thread)
	uint32_t read_ix_snap = (uint32_t)SDL_AtomicGet(&sampler->read_ix);

	// work with local write_ix (only audio thread writes this)
	uint32_t write_ix_local = (uint32_t)SDL_AtomicGet(&sampler->write_ix);

	// calculate available write space
	// usable capacity is buf_mask (input_buf_size - 1, one slot kept empty)
	uint32_t avail = (read_ix_snap - write_ix_local - 1) & sampler->buf_mask;

	uint32_t to_write = frames;
	if (to_write > avail) {
		S9_LOG_ERROR("osc_sampler buffer overflow: %u frames dropped", to_write - avail);
		to_write = avail;
	}

	// extract selected channel from interleaved input and write to input buffer
	// channel >= n_channels means sum all channels
	uint32_t ch = sampler->settings.channel;
	if (ch < n_channels) {
		for (uint32_t i = 0; i < to_write; i++) {
			sampler->input_buf[write_ix_local] = samples[i * n_channels + ch];
			write_ix_local = (write_ix_local + 1) & sampler->buf_mask;
		}
	} else {
		float scale = 1.0f / (float)n_channels;
		for (uint32_t i = 0; i < to_write; i++) {
			float sum = 0.0f;
			for (uint32_t c = 0; c < n_channels; c++) {
				sum += samples[i * n_channels + c];
			}
			sampler->input_buf[write_ix_local] = sum * scale;
			write_ix_local = (write_ix_local + 1) & sampler->buf_mask;
		}
	}

	// publish final write_ix with release semantics
	SDL_AtomicSet(&sampler->write_ix, (int)write_ix_local);

	return to_write;
}

uint32_t osc_sampler_read_pixels(osc_sampler_t *sampler, float *out, uint32_t max_pixels,
                                  float pixel_rate, float gain, int *triggered) {
	if (!sampler || !out || pixel_rate <= 0.0f) return 0;

	uint32_t write_ix_snap = (uint32_t)SDL_AtomicGet(&sampler->write_ix);

	// advance trigger state machine
	trigger_scan(sampler, write_ix_snap);

	// report sweep state
	if (triggered) *triggered = (sampler->trig_state == trig_state_sweep);

	// if not in sweep state, nothing to output
	if (sampler->trig_state != trig_state_sweep) return 0;

	// resample at pixel rate with gain
	float ratio = pixel_rate / (float)sampler->sample_rate;
	return resample_read(sampler, out, max_pixels, ratio, gain);
}

double osc_sampler_last_trigger(const osc_sampler_t *sampler) {
	return sampler ? sampler->last_trigger : 0.0;
}

void osc_sampler_sweep_complete(osc_sampler_t *sampler) {
	if (!sampler) return;
	sampler->holdoff_remaining = (uint32_t)(sampler->settings.trig_holdoff * sampler->sample_rate);
	sampler->trig_state = trig_state_wait;
}
