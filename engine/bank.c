// Omanoise v3 — instrument bank renderer.
#include "bank.h"
#include "dsp.h"
#include "padsynth.h"

#include <stdio.h>
#include <time.h>

// ------------------------------------------------------------- helpers

static int smp_alloc(sample_t *s, float seconds, float f0) {
  s->n = (int)(seconds * (float)RATE);
  s->d = calloc((size_t)s->n + 8, sizeof(float));
  s->loop0 = s->loop1 = 0;
  s->f0 = f0;
  return s->d ? 0 : -1;
}

static void smp_normalise(sample_t *s, float peak) {
  float mx = 0.0f;
  for (int i = 0; i < s->n; i++) { float a = fabsf(s->d[i]); if (a > mx) mx = a; }
  if (mx < 1e-9f) return;
  float g = peak / mx;
  for (int i = 0; i < s->n; i++) s->d[i] *= g;
}

// Raised-cosine fade-in; kills the switch-on click of every one-shot.
static void smp_fade_in(sample_t *s, float ms) {
  int f = (int)(ms * 0.001f * (float)RATE);
  if (f > s->n) f = s->n;
  for (int i = 0; i < f; i++)
    s->d[i] *= 0.5f - 0.5f * cosf(PI_F * (float)i / (float)f);
}

// Exponential tail-out over the last `ms`, ending at exactly zero.
static void smp_tail_out(sample_t *s, float ms) {
  int f = (int)(ms * 0.001f * (float)RATE);
  if (f > s->n) f = s->n;
  for (int i = 0; i < f; i++) {
    float x = (float)i / (float)f;                 // 0..1 across the tail
    float g = expf(-6.0f * x) * (1.0f - x);        // exp decay forced to 0
    s->d[s->n - f + i] *= g;
  }
}

// Make [loop0,loop1) seamless: equal-power crossfade of the material before
// loop0 into the last `xf` samples of the loop, so d[loop1] == d[loop0].
static void smp_make_loop(sample_t *s, float start_s, float end_s, float xf_s) {
  int l0 = (int)(start_s * (float)RATE);
  int l1 = (int)(end_s * (float)RATE);
  int xf = (int)(xf_s * (float)RATE);
  if (l1 > s->n) l1 = s->n;
  if (xf > l0) xf = l0;
  if (l1 - l0 < xf * 2 || xf < 16) { s->loop0 = s->loop1 = 0; return; }
  for (int i = 0; i < xf; i++) {
    float w = (float)i / (float)xf;                // 0..1
    float a = cosf(0.5f * PI_F * w);               // fades out the loop tail
    float b = sinf(0.5f * PI_F * w);               // fades in the pre-loop head
    int   t = l1 - xf + i;
    s->d[t] = s->d[t] * a + s->d[l0 - xf + i] * b;
  }
  // The 4-point interpolator straddles the wrap point, so the few samples at
  // and after loop1 must continue the loop, not the original tail.
  for (int k = 0; k < 4; k++)
    if (l1 + k < s->n + 8) s->d[l1 + k] = s->d[l0 + k];
  s->loop0 = l0;
  s->loop1 = l1;
}

// -------------------------------------------------------- oscillators

static inline float polyblep_saw(float *ph, float dt) {
  float t = *ph;
  float saw = 2.0f * t - 1.0f;
  if (t < dt)            { float x = t / dt;           saw -= (x + x - x * x - 1.0f); }
  else if (t > 1.0f - dt){ float x = (t - 1.0f) / dt;  saw -= (x * x + x + x + 1.0f); }
  t += dt;
  if (t >= 1.0f) t -= 1.0f;
  *ph = t;
  return saw;
}

// -------------------------------------------------------------- bass
// One saw plus a sine sub through two low-passes. v2 had a second saw walking
// +-5 cents against the first; that is exactly the "detune beating" of PLAN
// v3 §1.4, so it is gone, and so is the tanh that used to sit between the
// filters (no extra harmonics are wanted down here).

static void render_bass(sample_t *s, float f0) {
  float ph = 0.0f, sub = 0.0f;
  svf_t lp1, lp2, hp;
  svf_reset(&lp1); svf_reset(&lp2); svf_reset(&hp);
  svf_set(&lp1, 2.0f * f0, 0.7f);
  svf_set(&lp2, 2.0f * f0, 0.7f);
  svf_set(&hp, 0.55f * f0, 0.7f);
  for (int n = 0; n < s->n; n++) {
    float saw = polyblep_saw(&ph, f0 / (float)RATE);
    sub += (f0 * 0.5f) / (float)RATE; if (sub >= 1.0f) sub -= 1.0f;
    float y = 0.55f * saw + 0.5f * fsin(sub);
    y = svf_lp(&lp2, svf_lp(&lp1, y));
    s->d[n] = svf_hp(&hp, y);
  }
}

// -------------------------------------------------------- percussion
// Only used by the Focus pulse, which is default-off in v3.

static void render_kick(sample_t *s) {
  float ph = 0.0f;
  svf_t lp; svf_reset(&lp); svf_set(&lp, 200.0f, 0.7f);
  float amp = 1.0f;
  float kamp = expf(-1.0f / (0.25f * (float)RATE));
  for (int n = 0; n < s->n; n++) {
    float t = (float)n / (float)RATE;
    float f = 55.0f * (1.0f + 1.0f * expf(-t / 0.06f));   // one octave drop in 60 ms
    ph += f / (float)RATE; if (ph >= 1.0f) ph -= 1.0f;
    s->d[n] = svf_lp(&lp, fsin(ph) * amp);
    amp *= kamp;
  }
}

static void render_tick(sample_t *s, rng_t *rng) {
  svf_t lp; svf_reset(&lp); svf_set(&lp, 3000.0f, 0.7f);
  int burst = (int)(0.002f * (float)RATE);
  float amp = 1.0f;
  float kamp = expf(-1.0f / (0.040f * (float)RATE));
  for (int n = 0; n < s->n; n++) {
    float ex = n < burst ? rng_pm(rng) : 0.0f;
    s->d[n] = svf_lp(&lp, ex) * amp;
    amp *= kamp;
  }
}

// ---------------------------------------------------------------- api

static const float BASS_F0[BANK_BASS_N] = { 61.735f, 87.307f };

static double bank_now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int bank_render(bank_t *b, uint64_t seed) {
  rng_t rng; rng_seed(&rng, seed);
  memset(b, 0, sizeof(*b));
  double t0 = bank_now_ms();

  // ---- PADsynth tables. Two per profile: same spectrum, independent random
  // phases. L reads one, R the other, which is broad stereo with no frequency
  // difference anywhere — so there is nothing to beat (PLAN v3 §2.2).
  const int N = 1 << BANK_PAD_LOG2N;
  float *re = malloc((size_t)N * sizeof(float));
  float *im = malloc((size_t)N * sizeof(float));
  if (!re || !im) { free(re); free(im); return -1; }
  int rc = 0;
  for (int i = 0; i < BANK_PAD_LR; i++) {
    rc |= padsynth_render(&b->warm[i], BANK_PAD_LOG2N, BANK_PAD_HZ, PADP_WARM,
                          seed ^ (0x9E3779B9ull * (uint64_t)(i + 1)), re, im);
    rc |= padsynth_render(&b->glass[i], BANK_PAD_LOG2N, BANK_PAD_HZ, PADP_GLASS,
                          seed ^ (0x7F4A7C15ull * (uint64_t)(i + 3)), re, im);
  }
  free(re); free(im);
  if (rc) return -1;

  for (int i = 0; i < BANK_BASS_N; i++) {
    if (smp_alloc(&b->bass[i], 5.0f, BASS_F0[i])) return -1;
    render_bass(&b->bass[i], BASS_F0[i]);
    smp_normalise(&b->bass[i], 0.98f);
    smp_fade_in(&b->bass[i], 20.0f);
    smp_make_loop(&b->bass[i], 1.6f, 4.8f, 0.5f);
    smp_normalise(&b->bass[i], 0.98f);
  }

  if (smp_alloc(&b->kick, 0.45f, 0.0f)) return -1;
  render_kick(&b->kick);
  smp_normalise(&b->kick, 0.98f);
  smp_fade_in(&b->kick, 2.0f);
  smp_tail_out(&b->kick, 80.0f);

  if (smp_alloc(&b->tick, 0.09f, 0.0f)) return -1;
  render_tick(&b->tick, &rng);
  smp_normalise(&b->tick, 0.98f);
  smp_fade_in(&b->tick, 0.5f);
  smp_tail_out(&b->tick, 20.0f);

  size_t bytes = 0;
  const sample_t *all[] = { b->warm, b->glass, b->bass, &b->kick, &b->tick };
  const int counts[] = { BANK_PAD_LR, BANK_PAD_LR, BANK_BASS_N, 1, 1 };
  for (size_t g = 0; g < sizeof(all) / sizeof(all[0]); g++)
    for (int i = 0; i < counts[g]; i++) bytes += (size_t)all[g][i].n * sizeof(float);
  b->bytes = bytes;
  b->ms = bank_now_ms() - t0;
  return 0;
}

void bank_free(bank_t *b) {
  sample_t *all[] = { b->warm, b->glass, b->bass, &b->kick, &b->tick };
  const int counts[] = { BANK_PAD_LR, BANK_PAD_LR, BANK_BASS_N, 1, 1 };
  for (size_t g = 0; g < sizeof(all) / sizeof(all[0]); g++)
    for (int i = 0; i < counts[g]; i++) { free(all[g][i].d); all[g][i].d = NULL; all[g][i].n = 0; }
  b->bytes = 0;
}

const sample_t *bank_pick(const sample_t *arr, int n, float hz) {
  const sample_t *best = &arr[0];
  float bd = 1e9f;
  for (int i = 0; i < n; i++) {
    if (!arr[i].d || arr[i].f0 <= 0.0f) continue;
    float d = fabsf(log2f(hz / arr[i].f0));
    if (d < bd) { bd = d; best = &arr[i]; }
  }
  return best;
}

int bank_dump(const bank_t *b, const char *dir) {
  const sample_t *all[] = { b->warm, b->glass, b->bass, &b->kick, &b->tick };
  const char *names[] = { "warm", "glass", "bass", "kick", "tick" };
  const int counts[] = { BANK_PAD_LR, BANK_PAD_LR, BANK_BASS_N, 1, 1 };
  for (size_t g = 0; g < sizeof(all) / sizeof(all[0]); g++) {
    for (int i = 0; i < counts[g]; i++) {
      const sample_t *s = &all[g][i];
      if (!s->d) continue;
      char path[1024];
      snprintf(path, sizeof path, "%s/%s%d_%.0fhz.f32", dir, names[g], i, s->f0);
      FILE *f = fopen(path, "wb");
      if (!f) return -1;
      // Looped material is written as exactly one loop, so the dump can be
      // concatenated with itself to check the seam.
      int off = (s->loop1 > s->loop0) ? s->loop0 : 0;
      int len = (s->loop1 > s->loop0) ? (s->loop1 - s->loop0) : s->n;
      fwrite(s->d + off, sizeof(float), (size_t)len, f);
      fclose(f);
      fprintf(stderr, "bank %-6s %d  f0=%8.2f Hz  %6.3f s  loop %d..%d\n",
              names[g], i, s->f0, (float)len / (float)RATE, s->loop0, s->loop1);
    }
  }
  return 0;
}
