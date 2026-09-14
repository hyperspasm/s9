// The reader over frames of real spectra, without a display: which
// harmonics are shown, and steadily, what they read, their phases, and the
// distortion.
#include "greatest.h"
#include "harmonic_reader.h"
#include "fft_analyzer.h"
#include "test_signals.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FS 44100
#define FRAME 735   // samples per display frame at 60 Hz

static const harmonic_reader_config_t CONFIG = {
	.harmonics = 10, .search_min_bins = 3, .search_ratio = 0.01f, .shoulder_bins = 32,
	.prominence_db = 10.0f, .hysteresis_db = 3.0f, .hold_s = 0.5f, .note_jump_cents = 50.0f,
};

// an analyzer at the component's defaults feeding a reader, a frame at a time
typedef struct {
	fft_analyzer_t *fa;
	harmonic_reader_t *reader;
	float *db;
	uint32_t n, bins;
	unsigned offset;   // samples fed so far
} rig_t;

static void rig_init(rig_t *r) {
	r->fa = fft_analyzer_new(&(fft_analyzer_config_t){
		.resolution_hz = 2.0f, .window_function = WINDOW_FUNCTION_BLACKMAN_HARRIS, .mode = FFT_ANALYZER_MIX,
		.channel = S9_CHANNEL_ALL, .averaging_s = 1.0f,
	});
	fft_analyzer_prepare_to_stream(r->fa, &(s9_audio_stream_config_t){
		.audio_format = S9_FORMAT_FLOAT32, .sample_rate = FS, .channels = 1, .buffer_frames = 256, .samples_per_display_frame = FRAME,
	});
	r->n = fft_analyzer_fft_len(r->fa);
	r->bins = r->n / 2 + 1;
	r->db = malloc(sizeof(float) * r->bins);
	r->reader = malloc(harmonic_reader_alloc_size());
	harmonic_reader_init(r->reader, &CONFIG);
	harmonic_reader_set_window(r->reader, fft_analyzer_window(r->fa), r->n);
	r->offset = 0;
}

static void rig_free(rig_t *r) {
	fft_analyzer_free(&r->fa);
	free(r->reader);
	free(r->db);
}

// one frame of a signal (with a second added, when given), read as fundamental f0, locked or not
static void rig_frame(rig_t *r, const test_signal_t *s, const test_signal_t *added, double f0, int locked) {
	float *whole = calloc(r->offset + FRAME, sizeof(float)), frame[FRAME];
	test_signal_generate(s, whole, r->offset + FRAME, FS);
	memcpy(frame, whole + r->offset, sizeof(frame));
	if (added) {
		test_signal_generate(added, whole, r->offset + FRAME, FS);
		for (unsigned i = 0; i < FRAME; i++) frame[i] += whole[r->offset + i];
	}
	free(whole);
	r->offset += FRAME;
	fft_analyzer_enqueue_audio_data(r->fa, frame, sizeof(frame));
	fft_analyzer_update(r->fa);
	harmonic_power_db(fft_analyzer_power(r->fa, 0), r->bins, r->n, r->db);
	harmonic_reader_update(r->reader, r->db, fft_analyzer_spectrum(r->fa, 0), r->n, (double)FS / r->n, f0, locked,
						   fft_analyzer_average_fraction(r->fa), fft_analyzer_hop_seconds(r->fa));
}

// the difference between two angles in degrees, in (-180, 180]
static double angle_diff(double a, double b) {
	double d = fmod(a - b, 360.0);
	if (d < -180.0) d += 360.0;
	if (d > 180.0) d -= 360.0;
	return d;
}

// A sine in noise reads one harmonic, the fundamental at its level once the
// average has settled, and no distortion.
TEST sine_in_noise_reads_one_harmonic(void) {
	rig_t r;
	rig_init(&r);
	test_signal_t sine = { SIGNAL_SINE, 1000.0, 0, 0.5, 0.3, 0, 7, 0.02 };
	for (int i = 0; i < 300; i++) rig_frame(&r, &sine, NULL, 1000.0, 1);

	uint32_t count;
	const harmonic_reading_t *reads = harmonic_reader_readings(r.reader, &count);
	ASSERT_EQ(10u, count);
	ASSERT(reads[0].shown);
	ASSERT_IN_RANGE(-6.02, (double)reads[0].readout.db, 0.1);
	for (uint32_t k = 2; k <= count; k++) ASSERT_FALSE(reads[k - 1].shown);
	float thd;
	ASSERT_EQ(0, harmonic_reader_thd(r.reader, &thd));
	ASSERT_EQ_FMT(0.0f, thd, "%g");
	rig_free(&r);
	PASS();
}

// A sawtooth of ten harmonics reads all ten, their levels against the
// fundamental's, their phases, and the distortion they add up to. The
// generator's harmonics are sines, the even ones negated, so harmonic k
// sits at -90 + 90k degrees, plus 180 when even.
TEST sawtooth_reads_levels_phases_and_distortion(void) {
	rig_t r;
	rig_init(&r);
	test_signal_t saw = { SIGNAL_SAWTOOTH, 220.0, 0, 0.5, 0.0, 10, 0, 0 };
	for (int i = 0; i < 120; i++) rig_frame(&r, &saw, NULL, 220.0, 1);

	uint32_t count;
	const harmonic_reading_t *reads = harmonic_reader_readings(r.reader, &count);
	ASSERT_EQ(10u, count);
	for (uint32_t k = 1; k <= count; k++) {
		ASSERT(reads[k - 1].shown);
		ASSERT_IN_RANGE(-20.0 * log10(k), (double)(reads[k - 1].readout.db - reads[0].readout.db), 0.2);
		double expected = -90.0 + 90.0 * k + (k % 2 == 0 ? 180.0 : 0.0);
		ASSERT_IN_RANGE(0.0, angle_diff(reads[k - 1].phase_deg, expected), 1.0);
	}
	float thd;
	ASSERT_EQ(0, harmonic_reader_thd(r.reader, &thd));
	ASSERT_IN_RANGE(0.7415, (double)thd, 0.01);
	rig_free(&r);
	PASS();
}

// A harmonic a little above the floor is shown steadily once the average
// has settled, and the harmonics that are not there never are.
TEST weak_harmonic_is_shown_steadily(void) {
	rig_t r;
	rig_init(&r);
	// 3.7e-4 puts the third harmonic about 15 dB over the median of the floor
	// that noise of amplitude 0.01 makes under this window and resolution;
	// it is read about 12 dB above it
	test_signal_t sine = { SIGNAL_SINE, 220.0, 0, 0.5, 0.3, 0, 9, 0.01 };
	test_signal_t weak = { SIGNAL_SINE, 660.0, 0, 3.7e-4, 1.1, 0, 0, 0 };
	int changes = 0, empty_shown = 0, last = 0;
	for (int i = 0; i < 240; i++) {
		rig_frame(&r, &sine, &weak, 220.0, 1);
		uint32_t count;
		const harmonic_reading_t *reads = harmonic_reader_readings(r.reader, &count);
		if (i < 120) continue;   // the average settles
		if (count >= 3 && reads[2].shown != last) { changes++; last = reads[2].shown; }
		for (uint32_t k = 2; k <= count; k++)
			if (k != 3 && reads[k - 1].shown) empty_shown++;
	}
	ASSERT(last);
	ASSERT(changes <= 1);
	ASSERT_EQ(0, empty_shown);
	rig_free(&r);
	PASS();
}

// Without a lock nothing is read; a fundamental jumping to a new note
// restarts the reader, once.
TEST lock_and_new_note(void) {
	rig_t r;
	rig_init(&r);
	test_signal_t saw = { SIGNAL_SAWTOOTH, 220.0, 0, 0.5, 0.0, 10, 0, 0 };
	uint32_t count;
	float thd;
	rig_frame(&r, &saw, NULL, 220.0, 0);
	harmonic_reader_readings(r.reader, &count);
	ASSERT_EQ(0u, count);
	ASSERT_EQ(-1, harmonic_reader_thd(r.reader, &thd));

	for (int i = 0; i < 60; i++) rig_frame(&r, &saw, NULL, 220.0, 1);
	ASSERT_FALSE(harmonic_reader_restarted(r.reader));
	rig_frame(&r, &saw, NULL, 440.0, 1);
	ASSERT(harmonic_reader_restarted(r.reader));
	rig_frame(&r, &saw, NULL, 440.0, 1);
	ASSERT_FALSE(harmonic_reader_restarted(r.reader));
	rig_free(&r);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
	GREATEST_MAIN_BEGIN();
	RUN_TEST(sine_in_noise_reads_one_harmonic);
	RUN_TEST(sawtooth_reads_levels_phases_and_distortion);
	RUN_TEST(weak_harmonic_is_shown_steadily);
	RUN_TEST(lock_and_new_note);
	GREATEST_MAIN_END();
}
