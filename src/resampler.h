#ifndef resampler_h
#define resampler_h

#include <stdint.h>

typedef struct resampler resampler_t;

// Create resampler for multi-channel interleaved audio
// sample_rate: input sample rate in Hz (for reference)
// channels: number of channels (must be >= 1)
// buffer_size: desired buffer size in frames (will be rounded up to power of 2)
// Returns NULL on allocation failure or invalid parameters
resampler_t *resampler_new(uint32_t sample_rate, uint32_t channels, uint32_t buffer_size);

// Destroy resampler
void resampler_free(resampler_t **sampler);

// Audio thread: enqueue raw interleaved samples
// samples: interleaved audio frames [ch0, ch1, ..., ch0, ch1, ...]
// frames: number of frames to enqueue
// n_channels: number of channels in input (must match resampler's channel count)
// Returns number of frames actually enqueued (may be less if buffer full)
uint32_t resampler_enqueue(resampler_t *sampler, const float *samples, uint32_t frames, uint32_t n_channels);

// Render thread: read resampled interleaved data
// out: output buffer for interleaved frames
// max_frames: maximum frames to read
// rate: output_rate / input_rate (e.g., 2.0 for 2x upsampling, 0.5 for 2x downsampling)
// gain: amplitude multiplier applied to all channels
// Returns number of frames written (may be less if insufficient input data)
uint32_t resampler_read(resampler_t *sampler, float *out, uint32_t max_frames, float rate, float gain);

#endif /* resampler_h */
