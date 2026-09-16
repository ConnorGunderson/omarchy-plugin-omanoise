// Omanoise v3 — instrument bank: the material the engine plays back.
//
// v2 rendered eight synthesized one-shots here (7-saw supersaw pads, Risset
// and FM bells, Karplus-Strong plucks). v3 keeps only what survived the calm
// rules in docs/PLAN-sound-v3.md §1: two PADsynth wavetables per profile (one
// per channel, different random phases, identical spectra — stereo width with
// no detune and therefore no beating), a simple bass and the two pulse
// one-shots. Everything melodic is now sampled, via `sf2.[ch]`.
//
// Rendering happens once, in a worker thread, so the engine can answer `state`
// and play the noise bed immediately.
#ifndef OMANOISE_BANK_H
#define OMANOISE_BANK_H

#include "common.h"

typedef struct {
  float *d;          // mono f32, peak-normalised
  int    n;          // frames
  int    loop0;      // sustain loop start (frames); loop1 > loop0 == looped
  int    loop1;
  float  f0;         // pitch the sample was rendered at (Hz); 0 = unpitched
} sample_t;

#define BANK_BASS_N  2
#define BANK_PAD_LR  2          // two independent-phase tables per profile

// 2^18 samples = 5.46 s at 48 kHz, 1 MB per table.
#define BANK_PAD_LOG2N 18
#define BANK_PAD_HZ    220.0f

typedef struct {
  sample_t warm[BANK_PAD_LR];   // PADsynth, 1/n^1.6, bw 50 cents
  sample_t glass[BANK_PAD_LR];  // PADsynth, 1/n^2.4, bw 35 cents
  sample_t bass[BANK_BASS_N];   // saw + sine sub, looped
  sample_t kick;                // soft 55 Hz kick   (pulse, default off)
  sample_t tick;                // brushed off-beat tick
  size_t   bytes;               // total sample memory
  double   ms;                  // render time
} bank_t;

// Renders the whole bank. Returns 0 on success. Safe to call off the RT thread.
int  bank_render(bank_t *b, uint64_t seed);
void bank_free(bank_t *b);

// Nearest reference sample for a wanted pitch (log-distance).
const sample_t *bank_pick(const sample_t *arr, int n, float hz);

// Dump every sample as raw mono f32 into dir (for offline inspection).
int  bank_dump(const bank_t *b, const char *dir);

#endif
