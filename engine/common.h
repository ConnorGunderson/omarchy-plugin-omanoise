// Omanoise v2 — shared constants, RNG and small math helpers.
#ifndef OMANOISE_COMMON_H
#define OMANOISE_COMMON_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define RATE      48000
#define CH        2
#define BLOCK     64                              // control-rate block, samples
#define BLOCK_DT  ((float)BLOCK / (float)RATE)

#define PI_F      3.14159265358979f
#define TWOPI_F   6.28318530717959f

// ------------------------------------------------------------------ rng
// xorshift64*, one instance per thread/voice so nothing is shared.

typedef struct { uint64_t s; } rng_t;

static inline void rng_seed(rng_t *r, uint64_t s) {
  r->s = s ? s : 0x9E3779B97F4A7C15ull;
  for (int i = 0; i < 4; i++) { r->s ^= r->s << 13; r->s ^= r->s >> 7; r->s ^= r->s << 17; }
}
static inline uint32_t rng_u32(rng_t *r) {
  uint64_t x = r->s;
  x ^= x << 13; x ^= x >> 7; x ^= x << 17;
  r->s = x;
  return (uint32_t)(x >> 32);
}
static inline float rng_f(rng_t *r)  { return (float)(rng_u32(r) >> 8) * (1.0f / 16777216.0f); }
static inline float rng_pm(rng_t *r) { return rng_f(r) * 2.0f - 1.0f; }
static inline float rng_range(rng_t *r, float a, float b) { return a + (b - a) * rng_f(r); }
static inline float rng_log(rng_t *r, float a, float b)   { return a * powf(b / a, rng_f(r)); }
static inline int   rng_int(rng_t *r, int n) { return n <= 0 ? 0 : (int)(rng_u32(r) % (uint32_t)n); }

// ----------------------------------------------------------------- math

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float lerpf(float a, float b, float t)    { return a + (b - a) * t; }
static inline float db2lin(float db) { return powf(10.0f, db * 0.05f); }
static inline float lin2db(float g)  { return 20.0f * log10f(g > 1e-12f ? g : 1e-12f); }
static inline float midi2hz(float m) { return 440.0f * powf(2.0f, (m - 69.0f) * (1.0f / 12.0f)); }
// one-pole coefficient for a time constant in seconds at a given update rate
static inline float tau_k(float tau_s, float rate) {
  return tau_s <= 0.0f ? 1.0f : 1.0f - expf(-1.0f / (tau_s * rate));
}
static inline void slew(float *cur, float target, float k) { *cur += (target - *cur) * k; }

#endif
