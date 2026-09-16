#ifndef stereo_spectrum_h
#define stereo_spectrum_h

#include "spectrum.h"

// A stereo spectrum: the left channel's spectrum over the right's, one
// configuration for both. It owns two spectrums on channels 0 and 1, so
// the channel in the configuration it is given is not used.

typedef struct stereo_spectrum stereo_spectrum_t;

// create a stereo spectrum; NULL takes the spectrum's default configuration
stereo_spectrum_t *stereo_spectrum_new(const spectrum_config_t *config);

// destroy a stereo spectrum
void stereo_spectrum_free(stereo_spectrum_t **ss);

// render both spectrums, one over the other
void stereo_spectrum_render(stereo_spectrum_t *ss, SDL_Renderer *renderer);

// the configuration, and its replacement, applied to both at the next frame
const spectrum_config_t *stereo_spectrum_config(const stereo_spectrum_t *ss);
void stereo_spectrum_set_config(stereo_spectrum_t *ss, const spectrum_config_t *config);

// the frame's length in samples, the same for both; 0 before streaming
uint32_t stereo_spectrum_frame(const stereo_spectrum_t *ss);

// stop audio streaming
void stereo_spectrum_stop_streaming(stereo_spectrum_t *ss);

// prepare for audio streaming
void stereo_spectrum_prepare_to_stream(stereo_spectrum_t *ss, const s9_audio_stream_config_t *stream_config);

// call on a background thread to enqueue audio data
void stereo_spectrum_enqueue_audio_data(stereo_spectrum_t *ss, const void *data, uint32_t size);

#endif /* stereo_spectrum_h */
