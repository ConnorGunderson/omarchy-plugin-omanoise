// Omanoise v2 — DSP primitives.
//
//   svf_t      Cytomic linear trapezoidal state-variable filter (stable to Nyquist)
//   op_t       one-pole low-pass / DC blocker
//   bq_t       transposed direct form II biquad (K-weighting, master HP)
//   dline_t    power-of-two delay line with linear-interpolated reads
//   drift_t    Buchla-style value noise (cosine-interpolated held randoms)
//
// Everything hot is inline; only allocation and table setup live in dsp.c.
#ifndef OMANOISE_DSP_H
#define OMANOISE_DSP_H

#include "common.h"

// ------------------------------------------------------- lookup tables

extern float g_sin_tab[];      // 4096 + 1 entries, one period of sin(2*pi*x)
extern float g_hann_tab[];     // 2048 + 1 entries, 0.5 - 0.5*cos(2*pi*x)
void dsp_tables_init(void);

#define SIN_BITS 12
#define SIN_SIZE (1 << SIN_BITS)
#define HANN_SIZE 2048

// x in turns (any range)
static inline float fsin(float x) {
  x -= floorf(x);
  float p = x * (float)SIN_SIZE;
  int i = (int)p;
  float fr = p - (float)i;
  i &= (SIN_SIZE - 1);
  return g_sin_tab[i] + (g_sin_tab[i + 1] - g_sin_tab[i]) * fr;
}
// x in 0..1; window is 0 at both ends
static inline float fhann(float x) {
  if (x <= 0.0f || x >= 1.0f) return 0.0f;
  float p = x * (float)HANN_SIZE;
  int i = (int)p;
  float fr = p - (float)i;
  return g_hann_tab[i] + (g_hann_tab[i + 1] - g_hann_tab[i]) * fr;
}

// ------------------------------------------------------------ SVF

typedef struct { float ic1, ic2, a1, a2, a3, k; } svf_t;

static inline void svf_reset(svf_t *f) { f->ic1 = f->ic2 = 0.0f; }

static inline void svf_set(svf_t *f, float fc, float q) {
  fc = clampf(fc, 8.0f, 20000.0f);
  float g = tanf(PI_F * fc * (1.0f / (float)RATE));
  float k = 1.0f / clampf(q, 0.05f, 40.0f);
  f->a1 = 1.0f / (1.0f + g * (g + k));
  f->a2 = g * f->a1;
  f->a3 = g * f->a2;
  f->k  = k;
}

static inline float svf_lp(svf_t *f, float in) {
  float v3 = in - f->ic2;
  float v1 = f->a1 * f->ic1 + f->a2 * v3;
  float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
  f->ic1 = 2.0f * v1 - f->ic1;
  f->ic2 = 2.0f * v2 - f->ic2;
  return v2;
}
static inline float svf_hp(svf_t *f, float in) {
  float v3 = in - f->ic2;
  float v1 = f->a1 * f->ic1 + f->a2 * v3;
  float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
  f->ic1 = 2.0f * v1 - f->ic1;
  f->ic2 = 2.0f * v2 - f->ic2;
  return in - f->k * v1 - v2;
}
static inline float svf_bp(svf_t *f, float in) {
  float v3 = in - f->ic2;
  float v1 = f->a1 * f->ic1 + f->a2 * v3;
  float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
  f->ic1 = 2.0f * v1 - f->ic1;
  f->ic2 = 2.0f * v2 - f->ic2;
  return v1;
}

// ------------------------------------------------------- one-pole

typedef struct { float z, k; } op_t;
static inline void  op_set(op_t *p, float fc) { p->k = clampf(1.0f - expf(-TWOPI_F * fc / (float)RATE), 0.0f, 1.0f); }
static inline float op_lp(op_t *p, float in)  { p->z += p->k * (in - p->z); return p->z; }
static inline float op_hp(op_t *p, float in)  { p->z += p->k * (in - p->z); return in - p->z; }

// --------------------------------------------------------- biquad

typedef struct { float b0, b1, b2, a1, a2, z1, z2; } bq_t;

static inline float bq_run(bq_t *f, float in) {
  float out = f->b0 * in + f->z1;
  f->z1 = f->b1 * in - f->a1 * out + f->z2;
  f->z2 = f->b2 * in - f->a2 * out;
  return out;
}
static inline void bq_reset(bq_t *f) { f->z1 = f->z2 = 0.0f; }
void bq_highpass(bq_t *f, float fc, float q);
void bq_lowpass(bq_t *f, float fc, float q);
void bq_set(bq_t *f, float b0, float b1, float b2, float a1, float a2);

// -------------------------------------------------------- pink noise
// Paul Kellet's refined filter bank: white in, -3 dB/oct out, b[7] of state.
// Shared by the noise bed, the ocean foam and the fire glow.

static inline float pink_step(float *b, float w) {
  b[0] = 0.99886f * b[0] + w * 0.0555179f;
  b[1] = 0.99332f * b[1] + w * 0.0750759f;
  b[2] = 0.96900f * b[2] + w * 0.1538520f;
  b[3] = 0.86650f * b[3] + w * 0.3104856f;
  b[4] = 0.55000f * b[4] + w * 0.5329522f;
  b[5] = -0.7616f * b[5] - w * 0.0168980f;
  float out = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362f;
  b[6] = w * 0.115926f;
  return out * 0.11f;
}

// ----------------------------------------------------- delay line

typedef struct { float *buf; uint32_t mask, w; } dline_t;

int  dline_init(dline_t *d, uint32_t min_len);   // 0 on success
void dline_free(dline_t *d);
void dline_clear(dline_t *d);

static inline void dline_write(dline_t *d, float x) {
  d->buf[d->w] = x;
  d->w = (d->w + 1u) & d->mask;
}
// delay >= 1 returns the sample written `delay` writes ago
static inline float dline_read(const dline_t *d, uint32_t delay) {
  return d->buf[(d->w - delay) & d->mask];
}
static inline float dline_readf(const dline_t *d, float delay) {
  uint32_t i = (uint32_t)delay;
  float fr = delay - (float)i;
  float a = d->buf[(d->w - i) & d->mask];
  float b = d->buf[(d->w - i - 1u) & d->mask];
  return a + (b - a) * fr;
}

// Schroeder allpass living in a shared delay line: w[n] = x[n] + g*w[n-M],
// y[n] = -g*w[n] + w[n-M].  Read happens before the write, so `len` is exact.
static inline float dline_ap(dline_t *d, float in, uint32_t len, float g) {
  float wm = dline_read(d, len);
  float w  = in + g * wm;
  dline_write(d, w);
  return wm - g * w;
}
static inline float dline_apf(dline_t *d, float in, float len, float g) {
  float wm = dline_readf(d, len);
  float w  = in + g * wm;
  dline_write(d, w);
  return wm - g * w;
}

// -------------------------------------------------- drift generator
// Cosine-interpolated held randoms, output in [-1,1]. Stepped at control rate.

typedef struct { float a, b, t, period, base; int alt; rng_t rng; } drift_t;

void drift_init(drift_t *d, uint64_t seed, float period_s);
// `alt` makes each new target flip sign and keep a minimum magnitude, so the
// generator reliably swings between its extremes instead of occasionally
// dithering around the middle. Used for the macro "Animate" contour.
void drift_set_alternating(drift_t *d, int on);

static inline float drift_step(drift_t *d, float dt) {
  d->t += dt;
  if (d->t >= d->period) {
    d->t -= d->period;
    if (d->t >= d->period) d->t = 0.0f;
    d->a = d->b;
    if (d->alt) {
      float mag = 0.45f + 0.55f * rng_f(&d->rng);
      d->b = (d->a > 0.0f) ? -mag : mag;
    } else {
      d->b = rng_pm(&d->rng);
    }
    d->period = d->base * (0.7f + 0.6f * rng_f(&d->rng));
  }
  float u = d->t / d->period;
  // cos(pi*u) == sin(2*pi*(0.25 + u/2))
  float s = 0.5f - 0.5f * fsin(0.25f + 0.5f * u);
  return d->a + (d->b - d->a) * s;
}

#endif
