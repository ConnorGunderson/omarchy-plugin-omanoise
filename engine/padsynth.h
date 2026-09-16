// Omanoise v3 — PADsynth wavetable generator (Paul Nasca's public-domain
// algorithm) and the radix-2 FFT it is built on.
//
// A harmonic profile is smeared into a Gaussian band around every partial,
// given random phases and inverse-transformed once into a long looping table.
// The result is a lush pad with *no discrete beating* anywhere: the partials
// are continuous bands, not pairs of detuned sinusoids, so there is no
// amplitude modulation for the ear to hear as roughness (research-opensource,
// "beating detune = sensory dissonance").
//
// The FFT is also used offline by `--stats` for the envelope spectrum.
#ifndef OMANOISE_PADSYNTH_H
#define OMANOISE_PADSYNTH_H

#include "common.h"
#include "bank.h"

// In-place radix-2 complex FFT. `n` must be a power of two. `inverse` scales
// the result by 1/n. Allocation-free; caller owns re[] and im[].
void fft_radix2(float *re, float *im, int n, int inverse);

typedef enum { PADP_WARM = 0, PADP_GLASS = 1 } padprofile_t;

// Renders one PADsynth table of `log2n` points (e.g. 18 -> 262144 samples,
// 5.46 s at 48 kHz) into `s`, tuned so that playing it at rate 1.0 sounds at
// `base_hz`. The table loops seamlessly by construction; `s->loop0/loop1` are
// set with four guard samples on each side for the 4-point interpolator.
// `scratch_re`/`scratch_im` must each hold (1 << log2n) floats.
// Returns 0 on success.
int padsynth_render(sample_t *s, int log2n, float base_hz, padprofile_t prof,
                    uint64_t seed, float *scratch_re, float *scratch_im);

#endif
