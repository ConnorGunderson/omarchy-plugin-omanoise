// Omanoise v4 — master chain.
//
// HP 30 Hz -> 16 Hz AM on the 200 Hz..1 kHz band -> M/S width -> bus comp
// -> LP 9 kHz -> K-weighted LUFS auto-trim -> volume^2 -> soft limiter.
#ifndef OMANOISE_MASTER_H
#define OMANOISE_MASTER_H

#include "dsp.h"

typedef struct {
  bq_t  hpL, hpR;                 // 30 Hz, 2nd order
  svf_t amhpL, amhpR, amlpL, amlpR;
  float am_ph, am_depth, am_depth_cur;
  int   am_on;

  float width, width_cur;
  op_t  side_hp;

  float det, comp_gain, comp_gain_cur;   // bus compressor
  float comp_thresh_db, comp_ratio;

  op_t  lpL, lpR;                 // 9 kHz gentle roll-off

  bq_t  kL1, kL2, kR1, kR2;       // BS.1770 K-weighting
  float ms, ms_k;
  float lufs;
  float target, trim_db, trim_lin, trim_lin_cur;
  float settle;

  dline_t lookL, lookR;           // 5 ms look-ahead limiter
  uint32_t look;
  float lim_env, lim_gain;

  float vol_cur;
  float gr_db;                    // last compressor gain reduction, for reporting
} master_t;

int  master_init(master_t *m);
void master_free(master_t *m);
void master_reset(master_t *m);

// Control rate (once per BLOCK).
// `servo_on` freezes the loudness trim while the engine is fading in or out,
// so a pause does not wind the integrator up.
void master_control(master_t *m, float lufs_target, int am_on, float am_depth,
                    float width, float lp_hz, int servo_on);

// Audio rate. `vol` is the user volume (0..1); it is squared inside.
// (v3 also took a post-trim `post_gain` here, used only by Sleep's 20-minute
// thinning; the Sleep mode is gone in v4 and so is the parameter.)
void master_run(master_t *m, float inL, float inR, float vol,
                float *outL, float *outR);

#endif
