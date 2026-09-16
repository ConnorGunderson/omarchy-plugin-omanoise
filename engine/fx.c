// Omanoise v3 — effects implementation.
#include "fx.h"

// -------------------------------------------------------- Dattorro plate
// Paper lengths (29761 Hz reference) scaled by 1.61 for 48 kHz.

#define P_AP1   229u
#define P_AP2   172u
#define P_AP3   610u
#define P_AP4   446u
#define P_APL1 1082u
#define P_DL1  7169u
#define P_APL2 2898u
#define P_DL2  5989u
#define P_APR1 1462u
#define P_DR1  6789u
#define P_APR2 4276u
#define P_DR2  5092u
#define P_MOD    12.0f     // +-12 samples of tank allpass excursion

int plate_init(plate_t *p, uint64_t seed) {
  memset(p, 0, sizeof(*p));
  if (dline_init(&p->pre, (uint32_t)(0.100f * RATE))) return -1;
  if (dline_init(&p->ap1, P_AP1) || dline_init(&p->ap2, P_AP2) ||
      dline_init(&p->ap3, P_AP3) || dline_init(&p->ap4, P_AP4)) return -1;
  if (dline_init(&p->apL1, P_APL1 + 32) || dline_init(&p->dL1, P_DL1) ||
      dline_init(&p->apL2, P_APL2) || dline_init(&p->dL2, P_DL2)) return -1;
  if (dline_init(&p->apR1, P_APR1 + 32) || dline_init(&p->dR1, P_DR1) ||
      dline_init(&p->apR2, P_APR2) || dline_init(&p->dR2, P_DR2)) return -1;
  drift_init(&p->mod_l, seed | 1u, 9.3f);
  drift_init(&p->mod_r, seed * 2654435761ull + 17u, 11.7f);
  plate_set(p, 0.9f, 4000.0f, 9000.0f, 40.0f);
  return 0;
}

void plate_free(plate_t *p) {
  dline_free(&p->pre);
  dline_free(&p->ap1); dline_free(&p->ap2); dline_free(&p->ap3); dline_free(&p->ap4);
  dline_free(&p->apL1); dline_free(&p->dL1); dline_free(&p->apL2); dline_free(&p->dL2);
  dline_free(&p->apR1); dline_free(&p->dR1); dline_free(&p->apR2); dline_free(&p->dR2);
}

void plate_clear(plate_t *p) {
  dline_clear(&p->pre);
  dline_clear(&p->ap1); dline_clear(&p->ap2); dline_clear(&p->ap3); dline_clear(&p->ap4);
  dline_clear(&p->apL1); dline_clear(&p->dL1); dline_clear(&p->apL2); dline_clear(&p->dL2);
  dline_clear(&p->apR1); dline_clear(&p->dR1); dline_clear(&p->apR2); dline_clear(&p->dR2);
  p->bw_z = p->dampL = p->dampR = p->zl = p->zr = 0.0f;
}

void plate_set(plate_t *p, float decay, float damp_hz, float bandwidth_hz, float predelay_ms) {
  p->decay = clampf(decay, 0.1f, 0.985f);
  p->damp_k = clampf(1.0f - expf(-TWOPI_F * clampf(damp_hz, 200.0f, 16000.0f) / (float)RATE), 0.0f, 1.0f);
  p->bw_k = clampf(1.0f - expf(-TWOPI_F * clampf(bandwidth_hz, 200.0f, 18000.0f) / (float)RATE), 0.0f, 1.0f);
  uint32_t pre = (uint32_t)(predelay_ms * 0.001f * (float)RATE);
  if (pre < 1u) pre = 1u;
  if (pre > p->pre.mask - 4u) pre = p->pre.mask - 4u;
  p->pre_len = pre;
}

void plate_drift(plate_t *p, float dt) {
  p->mod_al = P_MOD * drift_step(&p->mod_l, dt);
  p->mod_ar = P_MOD * drift_step(&p->mod_r, dt);
}

void plate_run(plate_t *p, float in, float *outL, float *outR) {
  // Output taps (paper's table, scaled x1.61). Read before this sample's writes.
  float yl = dline_read(&p->dR1, 428u) + dline_read(&p->dR1, 4788u)
           - dline_read(&p->apR2, 3080u) + dline_read(&p->dR2, 3214u)
           - dline_read(&p->dL1, 3204u) - dline_read(&p->apL2, 301u)
           - dline_read(&p->dL2, 1716u);
  float yr = dline_read(&p->dL1, 568u) + dline_read(&p->dL1, 5839u)
           - dline_read(&p->apL2, 1977u) + dline_read(&p->dL2, 4303u)
           - dline_read(&p->dR1, 3399u) - dline_read(&p->apR2, 539u)
           - dline_read(&p->dR2, 195u);
  *outL = 0.6f * yl;
  *outR = 0.6f * yr;

  // Pre-delay and input bandwidth.
  float x = dline_read(&p->pre, p->pre_len);
  dline_write(&p->pre, in);
  p->bw_z += p->bw_k * (x - p->bw_z);
  x = p->bw_z;

  // Input diffusion.
  x = dline_ap(&p->ap1, x, P_AP1, 0.75f);
  x = dline_ap(&p->ap2, x, P_AP2, 0.75f);
  x = dline_ap(&p->ap3, x, P_AP3, 0.625f);
  x = dline_ap(&p->ap4, x, P_AP4, 0.625f);

  // Tank, figure of eight, de-fluttered by the two value-noise drifts.
  float ml = (float)P_APL1 + p->mod_al;
  float mr = (float)P_APR1 + p->mod_ar;

  float a = x + p->decay * p->zr;
  a = dline_apf(&p->apL1, a, ml, -0.7f);
  float da = dline_read(&p->dL1, P_DL1);
  dline_write(&p->dL1, a);
  p->dampL += p->damp_k * (da - p->dampL);
  a = p->dampL * p->decay;
  a = dline_ap(&p->apL2, a, P_APL2, 0.5f);
  float zl_new = dline_read(&p->dL2, P_DL2);
  dline_write(&p->dL2, a);

  float b = x + p->decay * p->zl;
  b = dline_apf(&p->apR1, b, mr, -0.7f);
  float db = dline_read(&p->dR1, P_DR1);
  dline_write(&p->dR1, b);
  p->dampR += p->damp_k * (db - p->dampR);
  b = p->dampR * p->decay;
  b = dline_ap(&p->apR2, b, P_APR2, 0.5f);
  float zr_new = dline_read(&p->dR2, P_DR2);
  dline_write(&p->dR2, b);

  p->zl = zl_new;
  p->zr = zr_new;
}
