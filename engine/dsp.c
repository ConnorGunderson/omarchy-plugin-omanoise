// Omanoise v2 — DSP primitives: tables, biquad design, delay lines, drift.
#include "dsp.h"

float g_sin_tab[SIN_SIZE + 1];
float g_hann_tab[HANN_SIZE + 1];

void dsp_tables_init(void) {
  static int done = 0;
  if (done) return;
  done = 1;
  for (int i = 0; i <= SIN_SIZE; i++)
    g_sin_tab[i] = sinf(2.0f * PI_F * (float)i / (float)SIN_SIZE);
  for (int i = 0; i <= HANN_SIZE; i++)
    g_hann_tab[i] = 0.5f - 0.5f * cosf(2.0f * PI_F * (float)i / (float)HANN_SIZE);
}

// ---------------------------------------------------------- biquads

void bq_set(bq_t *f, float b0, float b1, float b2, float a1, float a2) {
  f->b0 = b0; f->b1 = b1; f->b2 = b2; f->a1 = a1; f->a2 = a2;
  f->z1 = f->z2 = 0.0f;
}

void bq_highpass(bq_t *f, float fc, float q) {
  float w = 2.0f * PI_F * clampf(fc, 1.0f, 20000.0f) / (float)RATE;
  float cw = cosf(w), sw = sinf(w);
  float alpha = sw / (2.0f * q);
  float a0 = 1.0f + alpha;
  bq_set(f, (1.0f + cw) * 0.5f / a0, -(1.0f + cw) / a0, (1.0f + cw) * 0.5f / a0,
         (-2.0f * cw) / a0, (1.0f - alpha) / a0);
}

void bq_lowpass(bq_t *f, float fc, float q) {
  float w = 2.0f * PI_F * clampf(fc, 1.0f, 20000.0f) / (float)RATE;
  float cw = cosf(w), sw = sinf(w);
  float alpha = sw / (2.0f * q);
  float a0 = 1.0f + alpha;
  bq_set(f, (1.0f - cw) * 0.5f / a0, (1.0f - cw) / a0, (1.0f - cw) * 0.5f / a0,
         (-2.0f * cw) / a0, (1.0f - alpha) / a0);
}

// ------------------------------------------------------- delay lines

int dline_init(dline_t *d, uint32_t min_len) {
  uint32_t n = 16;
  while (n < min_len + 4u) n <<= 1;
  d->buf = calloc(n, sizeof(float));
  if (!d->buf) { d->mask = 0; d->w = 0; return -1; }
  d->mask = n - 1u;
  d->w = 0;
  return 0;
}

void dline_free(dline_t *d) { free(d->buf); d->buf = NULL; d->mask = 0; d->w = 0; }

void dline_clear(dline_t *d) {
  if (d->buf) memset(d->buf, 0, ((size_t)d->mask + 1) * sizeof(float));
  d->w = 0;
}

// ------------------------------------------------------------ drift

void drift_set_alternating(drift_t *d, int on) { d->alt = on; }

void drift_init(drift_t *d, uint64_t seed, float period_s) {
  memset(d, 0, sizeof(*d));
  rng_seed(&d->rng, seed);
  d->base = period_s;
  d->period = period_s * (0.7f + 0.6f * rng_f(&d->rng));
  d->a = rng_pm(&d->rng);
  d->b = rng_pm(&d->rng);
  d->t = rng_f(&d->rng) * d->period;
}
