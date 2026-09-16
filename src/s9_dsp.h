#ifndef s9_dsp_h
#define s9_dsp_h

#include "stdint.h"
#include <stddef.h>

typedef enum {
	S9_SAMPLE_NONE = 0,
	S9_SAMPLE_FLOAT32,
} s9_sample_type_t;

typedef struct {
	s9_sample_type_t sample_type;
	uint32_t 		 bytes_per_sample;
} s9_audio_format_t;

extern const s9_audio_format_t S9_FORMAT_FLOAT32;

#define S9_AUDIO_FORMAT_EQUALS(fmt1, fmt2) \
	((fmt1).sample_type == (fmt2).sample_type && \
	 (fmt1).bytes_per_sample == (fmt2).bytes_per_sample)

// Where one channel of the stream may be chosen: every channel instead
#define S9_CHANNEL_ALL UINT32_MAX

// The name a display shows for its channel: "CH1" for a channel, and
// "CH1+CH2" for every one of a stream's channels together; empty when the
// stream has none. Into buf, which holds size bytes.
void s9_channel_label(uint32_t channel, uint32_t channels, char *buf, size_t size);

typedef struct {
	s9_audio_format_t 	audio_format;				// sample format
	uint32_t 		  	sample_rate;				// audio sampling rate in Hz
	uint32_t 		  	channels;					// number of channels in stream
	uint32_t 			buffer_frames;				// number of frames per streaming buffer
	uint32_t 			samples_per_display_frame;	// number of samples (rounding up) per display frame
} s9_audio_stream_config_t;

#define S9_AUDIO_STREAM_CONFIG_EQUALS(cfg1, cfg2) \
	(S9_AUDIO_FORMAT_EQUALS((cfg1).audio_format, (cfg2).audio_format) && \
	 (cfg1).sample_rate == (cfg2).sample_rate && \
	 (cfg1).channels == (cfg2).channels && \
	 (cfg1).buffer_frames == (cfg2).buffer_frames && \
	 (cfg1).samples_per_display_frame == (cfg2).samples_per_display_frame)

#endif /* s9_dsp_h */
