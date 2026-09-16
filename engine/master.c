// Omanoise v4 — master chain implementation.
#include "master.h"

#define LIMIT_CEIL 0.83f       // ~-1.6 dBFS, leaves room for inter-sample peaks
#define LOOKAHEAD  240u        // 5 ms

int master_init(master_t *m) {
  memset(m, 0, sizeof(*m));
  bq_highpass(&m->hpL, 30.0f, 0.707f);
  bq_highpass(&m->hpR, 30.0f, 0.707f);
  svf_set(&m->amhpL, 200.0f, 0.707f); svf_set(&m->amhpR, 200.0f, 0.707f);
  svf_set(&m->amlpL, 1000.0f, 0.707f); svf_set(&m->amlpR, 1000.0f, 0.707f);
  op_set(&m->lpL, 9000.0f);
  op_set(&m->lpR, 9000.0f);
  op_set(&m->side_hp, 200.0f);

  // BS.1770-4 K-weighting at 48 kHz.
  bq_set(&m->kL1, 1.53512485958697f, -2.69169618940638f, 1.19839281085285f,
                 -1.69065929318241f, 0.73248077421585f);
  bq_set(&m->kR1, 1.53512485958697f, -2.69169618940638f, 1.19839281085285f,
                 -1.69065929318241f, 0.73248077421585f);
  bq_set(&m->kL2, 1.0f, -2.0f, 1.0f, -1.99004745483398f, 0.99007225036621f);
  bq_set(&m->kR2, 1.0f, -2.0f, 1.0f, -1.99004745483398f, 0.99007225036621f);
  m->ms_k = tau_k(3.0f, (float)RATE);      // short-term-ish integration
  m->ms = 0.0f;
  m->lufs = -70.0f;
  m->target = -23.0f;
  m->trim_db = 0.0f;
  m->trim_lin = m->trim_lin_cur = 1.0f;
  m->settle = 0.0f;

  m->width = m->width_cur = 1.3f;
  m->comp_thresh_db = -10.0f;
  m->comp_ratio = 1.5f;
  m->comp_gain = m->comp_gain_cur = 1.0f;
  m->lim_gain = 1.0f;
  m->vol_cur = 0.0f;
  m->look = LOOKAHEAD;
  if (dline_init(&m->lookL, LOOKAHEAD + 8) || dline_init(&m->lookR, LOOKAHEAD + 8)) return -1;
  return 0;
}

void master_free(master_t *m) { dline_free(&m->lookL); dline_free(&m->lookR); }

void master_reset(master_t *m) {
  bq_reset(&m->hpL); bq_reset(&m->hpR);
  bq_reset(&m->kL1); bq_reset(&m->kL2); bq_reset(&m->kR1); bq_reset(&m->kR2);
  svf_reset(&m->amhpL); svf_reset(&m->amhpR); svf_reset(&m->amlpL); svf_reset(&m->amlpR);
  dline_clear(&m->lookL); dline_clear(&m->lookR);
  m->ms = 0.0f; m->lufs = -70.0f; m->settle = 0.0f;
  m->trim_db = 0.0f; m->trim_lin = m->trim_lin_cur = 1.0f;
  m->lim_env = 0.0f; m->lim_gain = 1.0f; m->det = 0.0f;
  m->comp_gain = m->comp_gain_cur = 1.0f;
}

void master_control(master_t *m, float lufs_target, int am_on, float am_depth,
                    float width, float lp_hz, int servo_on) {
  m->target = lufs_target;
  m->am_on = am_on;
  m->am_depth = clampf(am_depth, 0.0f, 0.9f);
  m->width = clampf(width, 0.0f, 2.0f);
  op_set(&m->lpL, lp_hz);
  op_set(&m->lpR, lp_hz);

  // --- bus compressor gain (1.5:1, ~1-2 dB of reduction on a loud mix)
  float det_db = lin2db(m->det);
  float over = det_db - m->comp_thresh_db;
  float want_db = over > 0.0f ? -over * (1.0f - 1.0f / m->comp_ratio) : 0.0f;
  float cur_db = lin2db(m->comp_gain);
  float k = (want_db < cur_db) ? tau_k(0.040f, 1.0f / BLOCK_DT)    // attack
                               : tau_k(0.800f, 1.0f / BLOCK_DT);   // release
  cur_db += (want_db - cur_db) * k;
  m->gr_db = cur_db;
  m->comp_gain = db2lin(cur_db);

  // --- K-weighted loudness and the slow auto-trim
  m->lufs = (m->ms > 1e-12f) ? (-0.691f + 10.0f * log10f(m->ms)) : -70.0f;
  m->settle += BLOCK_DT;
  if (servo_on && m->lufs > -60.0f) {
    // Integral control: the measurement is taken after the trim, so pushing
    // trim by the error converges on target. Fast at first, then very slow.
    float tau = (m->settle < 20.0f) ? lerpf(2.0f, 30.0f, m->settle / 20.0f) : 30.0f;
    float err = m->target - m->lufs;
    m->trim_db += err * (BLOCK_DT / tau);
    m->trim_db = clampf(m->trim_db, -6.0f, 6.0f);
  }
  m->trim_lin = db2lin(m->trim_db);
}

void master_run(master_t *m, float inL, float inR, float vol,
                float *outL, float *outR) {
  // smooth the control-rate gains across the block
  m->comp_gain_cur += (m->comp_gain - m->comp_gain_cur) * 0.002f;
  m->trim_lin_cur  += (m->trim_lin  - m->trim_lin_cur)  * 0.0005f;
  m->width_cur     += (m->width     - m->width_cur)     * 0.002f;
  m->vol_cur       += (vol * vol - m->vol_cur) * 0.0008f;

  float l = bq_run(&m->hpL, inL);
  float r = bq_run(&m->hpR, inR);

  // --- 16 Hz amplitude modulation on the 200 Hz - 1 kHz band
  m->am_ph += 16.0f * (1.0f / (float)RATE);
  if (m->am_ph >= 1.0f) m->am_ph -= 1.0f;
  float dtgt = m->am_on ? m->am_depth : 0.0f;
  m->am_depth_cur += (dtgt - m->am_depth_cur) * 0.0005f;
  if (m->am_depth_cur > 0.0005f) {
    // 1 - d*(0.5 - 0.5*cos(2*pi*16*t));  cos(x) = sin(x + pi/2)
    float mod = 1.0f - m->am_depth_cur * (0.5f - 0.5f * fsin(m->am_ph + 0.25f));
    float bl = svf_lp(&m->amlpL, svf_hp(&m->amhpL, l));
    float br = svf_lp(&m->amlpR, svf_hp(&m->amhpR, r));
    l = (l - bl) + bl * mod;
    r = (r - br) + br * mod;
  } else {
    // keep the filter states warm so switching the toggle is click-free
    (void)svf_lp(&m->amlpL, svf_hp(&m->amhpL, l));
    (void)svf_lp(&m->amlpR, svf_hp(&m->amhpR, r));
  }

  // --- mid/side width, side high-passed so the bass stays mono
  float mid = 0.5f * (l + r);
  float side = 0.5f * (l - r);
  side = op_hp(&m->side_hp, side) * m->width_cur;
  l = mid + side;
  r = mid - side;

  // --- bus compressor
  float pk = fmaxf(fabsf(l), fabsf(r));
  m->det += (pk - m->det) * (pk > m->det ? 0.002f : 0.00002f);
  l *= m->comp_gain_cur;
  r *= m->comp_gain_cur;

  // --- gentle top roll-off
  l = op_lp(&m->lpL, l);
  r = op_lp(&m->lpR, r);

  // --- loudness trim, then the K-weighted measurement of the trimmed signal
  l *= m->trim_lin_cur;
  r *= m->trim_lin_cur;
  float kl = bq_run(&m->kL2, bq_run(&m->kL1, l));
  float kr = bq_run(&m->kR2, bq_run(&m->kR1, r));
  m->ms += (kl * kl + kr * kr - m->ms) * m->ms_k;

  // --- user volume
  l *= m->vol_cur;
  r *= m->vol_cur;

  // --- 5 ms look-ahead soft limiter
  float peak = fmaxf(fabsf(l), fabsf(r));
  if (peak > m->lim_env) m->lim_env = peak;
  else m->lim_env += (peak - m->lim_env) * 0.0002f;
  float want = (m->lim_env > LIMIT_CEIL) ? LIMIT_CEIL / m->lim_env : 1.0f;
  m->lim_gain += (want - m->lim_gain) * (want < m->lim_gain ? 0.02f : 0.0004f);

  float dl = dline_read(&m->lookL, m->look);
  float dr = dline_read(&m->lookR, m->look);
  dline_write(&m->lookL, l);
  dline_write(&m->lookR, r);
  dl *= m->lim_gain;
  dr *= m->lim_gain;

  // soft knee above 0.7 of the ceiling
  const float knee = 0.7f * LIMIT_CEIL, span = 0.3f * LIMIT_CEIL;
  float al = fabsf(dl);
  if (al > knee) dl = (dl < 0.0f ? -1.0f : 1.0f) * (knee + span * tanhf((al - knee) / span));
  float ar = fabsf(dr);
  if (ar > knee) dr = (dr < 0.0f ? -1.0f : 1.0f) * (knee + span * tanhf((ar - knee) / span));

  *outL = dl;
  *outR = dr;
}
