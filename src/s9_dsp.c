#include "s9_dsp.h"

#include <stdio.h>

const s9_audio_format_t S9_FORMAT_FLOAT32 = {
	.sample_type = S9_SAMPLE_FLOAT32,
	.bytes_per_sample = 4,
};


void s9_channel_label(uint32_t channel, uint32_t channels, char *buf, size_t size) {
	if (size == 0) return;
	buf[0] = '\0';
	if (channels == 0) return;
	if (channel < channels) {
		snprintf(buf, size, "CH%u", channel + 1);
		return;
	}
	size_t pos = 0;
	for (uint32_t i = 0; i < channels && pos + 1 < size; i++) {
		int n = snprintf(buf + pos, size - pos, "%sCH%u", i > 0 ? "+" : "", i + 1);
		if (n < 0) return;
		pos += (size_t)n;
	}
}
