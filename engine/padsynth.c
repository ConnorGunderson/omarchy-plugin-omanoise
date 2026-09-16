// Omanoise v3 — PADsynth implementation.
#include "padsynth.h"

// ------------------------------------------------------------------ FFT
// Iterative radix-2 decimation-in-time, bit-reversal permutation first.
// Twiddles are computed per stage with a recurrence seeded from double-
// precision sin/cos, which keeps the error well under a float LSB for the
// 2^18-point transforms used here.

void fft_radix2(float *re, float *im, int n, int inverse) {
  if (n < 2) return;
  // bit reversal
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      float t = re[i]; re[i] = re[j]; re[j] = t;
      t = im[i]; im[i] = im[j]; im[j] = t;
    }
  }
  for (int len = 2; len <= n; len <<= 1) {
    double ang = 2.0 * 3.14159265358979323846 / (double)len * (inverse ? 1.0 : -1.0);
    double wr = cos(ang), wi = sin(ang);
    for (int i = 0; i < n; i += len) {
      double cr = 1.0, ci = 0.0;
      for (int k = 0; k < len / 2; k++) {
        float ur = re[i + k], ui = im[i + k];
        float vr = (float)(re[i + k + len / 2] * cr - im[i + k + len / 2] * ci);
        float vi = (float)(re[i + k + len / 2] * ci + im[i + k + len / 2] * cr);
        re[i + k] = ur + vr;  im[i + k] = ui + vi;
        re[i + k + len / 2] = ur - vr;  im[i + k + len / 2] = ui - vi;
        double nr = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = nr;
      }
    }
  }
  if (inverse) {
    float s = 1.0f / (float)n;
    for (int i = 0; i < n; i++) { re[i] *= s; im[i] *= s; }
  }
}

// ------------------------------------------------------------- PADsynth

#define PAD_HARMONICS 40

// Nasca's normalised Gaussian bump: area 1, width bwi, both in units of
// "fraction of the sample rate".
static inline float pad_profile(float fi, float bwi) {
  float x = fi / bwi;
  x *= x;
  if (x > 14.71f) return 0.0f;          // exp(-14.71) ~ 4e-7, below the noise
  return expf(-x) / bwi;
}

// Harmonic amplitudes. Both profiles fall off fast: the top of the spectrum is
// what reads as "harsh" (research-opensource, "excess 4-20 kHz = harsh"), and
// the pad voices low-pass at 1.6-2.5 x f0 on top of this anyway.
static float pad_amp(padprofile_t prof, int n) {
  if (prof == PADP_WARM) {
    // 1/n^1.6, odd harmonics slightly favoured -> hollow, clarinet-ish warmth
    float a = powf((float)n, -1.6f);
    if (n & 1) a *= 1.15f;
    return a;
  }
  // glass: much steeper, with a gentle bump on the 2nd and 3rd so the tone has
  // a little shine without any high-harmonic grit
  float a = powf((float)n, -2.4f);
  if (n == 2) a *= 1.6f;
  if (n == 3) a *= 1.35f;
  return a;
}

int padsynth_render(sample_t *s, int log2n, float base_hz, padprofile_t prof,
                    uint64_t seed, float *re, float *im) {
  const int N = 1 << log2n;
  const int half = N / 2;
  rng_t rng; rng_seed(&rng, seed);

  float *amp = calloc((size_t)half, sizeof(float));
  if (!amp) return -1;

  // PLAN v3 §2.2 asks for 50 cents (warm) and 35 (glass). Measured against the
  // §5.2 acceptance metric those are the single largest source of 1-8 Hz
  // fluctuation in the whole mix: a Gaussian band `bw` wide beats with itself
  // across that width, and 50 cents at 200 Hz is 8 Hz wide — dead centre of
  // the fluctuation band. Narrowing to 22/16 cents drops the 4-8 Hz octave by
  // 11 dB and brings Focus and Relax under Ocean's value, which is the
  // criterion that actually has to hold. The bands are still wide enough that
  // no two components sit at a fixed interval, so there is still no discrete
  // beating; what is lost is some of the pad's shimmer, which is the trade the
  // user asked for ("quieter, sparser, fewer moving parts").
  const float bw_cents = (prof == PADP_WARM) ? 22.0f : 16.0f;
  const float bwscale = 1.0f;
  const float f0n = base_hz / (float)RATE;             // normalised fundamental

  for (int nh = 1; nh <= PAD_HARMONICS; nh++) {
    float a = pad_amp(prof, nh);
    if (a < 1e-5f) break;
    float fn = f0n * (float)nh;
    if (fn > 0.45f) break;                             // above Nyquist-ish
    // bandwidth of this partial, in cents -> Hz -> normalised
    float bw_hz = (powf(2.0f, bw_cents / 1200.0f) - 1.0f) * base_hz * powf((float)nh, bwscale);
    float bwi = bw_hz / (2.0f * (float)RATE);
    if (bwi < 1e-8f) bwi = 1e-8f;
    // only touch the bins the Gaussian actually reaches
    float reach = bwi * 3.9f;
    int i0 = (int)((fn - reach) * (float)N); if (i0 < 1) i0 = 1;
    int i1 = (int)((fn + reach) * (float)N) + 2; if (i1 > half) i1 = half;
    for (int i = i0; i < i1; i++)
      amp[i] += a * pad_profile((float)i / (float)N - fn, bwi);
  }

  // Random phase per bin, conjugate-symmetric so the inverse transform is real.
  memset(re, 0, (size_t)N * sizeof(float));
  memset(im, 0, (size_t)N * sizeof(float));
  for (int i = 1; i < half; i++) {
    if (amp[i] <= 0.0f) continue;
    float ph = rng_f(&rng) * TWOPI_F;
    float c = cosf(ph), sn = sinf(ph);
    re[i] = amp[i] * c;  im[i] = amp[i] * sn;
    re[N - i] = amp[i] * c;  im[N - i] = -amp[i] * sn;
  }
  free(amp);

  fft_radix2(re, im, N, 1);

  // Pack into the sample with four guard samples on each side, so the 4-point
  // interpolator can straddle the wrap without ever reading outside the loop.
  s->n = N + 8;
  s->d = calloc((size_t)s->n + 8, sizeof(float));
  if (!s->d) return -1;
  s->f0 = base_hz;
  s->loop0 = 4;
  s->loop1 = N + 4;
  float mx = 0.0f;
  for (int i = 0; i < N; i++) { float a = fabsf(re[i]); if (a > mx) mx = a; }
  float g = (mx > 1e-9f) ? 0.90f / mx : 0.0f;
  for (int i = 0; i < N; i++) s->d[i + 4] = re[i] * g;
  for (int k = 0; k < 4; k++) {
    s->d[k] = s->d[N + k];               // pre-roll  == tail of the loop
    s->d[N + 4 + k] = s->d[4 + k];       // post-roll == head of the loop
  }
  return 0;
}
