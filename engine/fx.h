// Omanoise v3 — effects: the Dattorro plate, and nothing else.
//
// v2 also had a Solina triple chorus (0.9 Hz and 5.9 Hz LFOs), a shimmer bus
// (octave-up shifter into a second plate) and a ping-pong delay. All three are
// gone: the chorus LFOs sit squarely in the fluctuation/roughness bands that
// PLAN v3 §1.1 forbids, the shimmer was the loudest thing in the mode the user
// disliked, and the delay multiplied every event into extra onsets.
#ifndef OMANOISE_FX_H
#define OMANOISE_FX_H

#include "dsp.h"

// ------------------------------------------------------- Dattorro plate

typedef struct {
  dline_t pre, ap1, ap2, ap3, ap4;
  dline_t apL1, dL1, apL2, dL2;
  dline_t apR1, dR1, apR2, dR2;
  float bw_z, dampL, dampR;
  float zl, zr;
  // The paper modulates the first tank allpass of each side with a 0.7-1 Hz
  // sine. That is a periodic modulation reaching the audio, which v3 does not
  // allow anywhere, so the excursion comes from two slow independent
  // value-noise drifts instead (equivalent de-fluttering, no line spectrum).
  drift_t mod_l, mod_r;
  float mod_al, mod_ar;
  // control-rate parameters
  float decay;        // 0.5 .. 0.99
  float damp_k;       // one-pole coefficient for the tank damping filter
  float bw_k;         // input bandwidth
  uint32_t pre_len;   // pre-delay in samples
} plate_t;

int  plate_init(plate_t *p, uint64_t seed);
void plate_free(plate_t *p);
void plate_clear(plate_t *p);
void plate_set(plate_t *p, float decay, float damp_hz, float bandwidth_hz, float predelay_ms);
// Control rate: advances the two drift generators that de-flutter the tank.
void plate_drift(plate_t *p, float dt);
void plate_run(plate_t *p, float in, float *outL, float *outR);

#endif
