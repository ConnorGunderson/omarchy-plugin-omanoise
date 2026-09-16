// Omanoise v5 — the rain generator and the recorded-sound player.
//
// Everything here is realtime-safe: fixed-size voice pools, no allocation, no
// locking, no printing. Control-rate work (filter coefficients, event rates,
// slow envelopes) happens once per 64-sample block in env_control()/
// rec_control(); the audio-rate functions only run filters, voices and sample
// reads.
//
// Modulation policy (PLAN v3 §1.1, audited in ACCEPTANCE v5): there is no
// periodic oscillator anywhere in here. Slow movement comes from `drift_t`
// value noise whose base periods are 16 s and longer (<= 0.09 Hz), the rain
// patter is a Poisson process (flat spectrum, no line), and the recorded player
// schedules random segments of random length — nothing in it repeats.
#include "env.h"

const char *const ENV_NAME[ENV_N] = { "ocean", "rain", "fire", "wind", "stream", "birds" };
const char *const ENV_FILE[ENV_N] = { "ocean", "rain", "fireplace", "wind", "stream", "birds" };

int env_from_name(const char *s) {
  if (!s) return -1;
  for (int i = 0; i < ENV_N; i++) if (strcmp(s, ENV_NAME[i]) == 0) return i;
  return -1;
}

// Geometric centre and half-width in octaves of a [lo,hi] sweep range.
static inline float sweep_hz(float lo, float hi, float x) {
  float c = sqrtf(lo * hi);
  float half = log2f(hi / lo) * 0.5f;
  return clampf(c * powf(2.0f, half * x), lo, hi);
}

// ---------------------------------------------------------------- init

void env_init(envbank_t *e, uint64_t seed) {
  memset(e, 0, sizeof(*e));

  rain_t *R = &e->rain;
  for (int c = 0; c < 2; c++) {
    rng_seed(&R->rng[c], seed + 0x1111u * (uint64_t)(c + 1));
    svf_set(&R->bp[c], 530.0f, 1.0f);
    svf_set(&R->lp[c], 3000.0f, 0.7f);
    svf_set(&R->hhp[c], 1800.0f, 0.7f);
    svf_set(&R->hlp[c], 6500.0f, 0.7f);
    svf_set(&R->hlp2[c], 6500.0f, 0.7f);
    svf_set(&R->dhp[c], 1000.0f, 0.7f);
  }
  drift_init(&R->centre_dr, seed + 0x2001u, 26.0f);   // 0.038 Hz base
  drift_init(&R->shower_dr, seed + 0x2002u, 16.0f);   // 0.063 Hz base
  drift_init(&R->pan_dr, seed + 0x2003u, 23.0f);
  // Alternating: every level envelope in the bank swings to the other side of
  // zero on each new segment instead of being free to wander to one side for a
  // minute at a time. The segment lengths and magnitudes stay random, so this
  // is not a periodic modulation - it just stops a 180 s render from landing
  // 5 dB off its own long-run mean, which would otherwise eat the +-6 dB the
  // loudness servo has to work with.
  drift_set_alternating(&R->shower_dr, 1);
  R->p_hit = 1200.0f / (float)RATE;
  R->hit_amp = 0.5f;
  R->hiss_g = db2lin(-16.0f);
  // Droplet chirps removed: a 12-20 ms downward chirp is the textbook bubble
  // sound and the user heard exactly that ("a bubble popping every now and
  // then"). Rain is now the patter bed and the hiss only.
  R->drop_g = 0.0f;
  R->p_drop = 0.0f;

  for (int i = 0; i < ENV_N; i++) {
    rec_t *r = &e->rec[i];
    rng_seed(&r->rng, seed + 0x7A1Du * (uint64_t)(i + 1));
    for (int c = 0; c < 2; c++) {
      svf_set(&r->hp[c], 40.0f, 0.7f);
      svf_set(&r->lp[c], 9000.0f, 0.7f);
    }
    // 30 s base => 0.024-0.048 Hz, under the 0.05 Hz the plan allows.
    drift_init(&r->lev_dr, seed + 0x7B00u * (uint64_t)(i + 1), 30.0f);
    drift_set_alternating(&r->lev_dr, 1);
    r->lev_g = 1.0f;
  }
}

// -------------------------------------------------------- control rate

void env_control(envbank_t *e, float dt, float bright, float intensity, const float *lev) {
  rain_t *R = &e->rain;
  // The patter rate follows the slider and Intensity together (PLAN §1.3);
  // the per-impulse amplitude is scaled by 1/sqrt(rate) so a denser shower
  // is denser, not louder.
  float x = clampf(0.5f * lev[ENV_RAIN] + 0.5f * intensity, 0.0f, 1.0f);
  float rate = 800.0f + 800.0f * x;
  R->p_hit = rate / (float)RATE;
  R->hit_amp = 0.50f * sqrtf(1200.0f / rate);

  float centre = sweep_hz(350.0f, 800.0f, clampf(drift_step(&R->centre_dr, dt)
                                                 + bright * 0.30f, -1.0f, 1.0f));
  svf_set(&R->bp[0], centre, 1.0f);
  // The right channel sits a little above the left so the patter is wide
  // without the two sides being a filtered copy of each other.
  svf_set(&R->bp[1], clampf(centre * 1.12f, 350.0f, 900.0f), 1.0f);
  float lp = clampf(3000.0f * powf(2.0f, bright * 0.4f), 1800.0f, 6000.0f);
  svf_set(&R->lp[0], lp, 0.7f);
  svf_set(&R->lp[1], lp, 0.7f);
  // Two cascaded 2-pole sections at 6.5 kHz rather than one: with a single
  // section the sizzle above the corner put the >4 kHz band within 2 dB of
  // the 200 Hz-1 kHz band, and rain has to sit 4 dB under it (PLAN v4 §4.4).
  float hlp = clampf(6500.0f * powf(2.0f, bright * 0.4f), 4000.0f, 12000.0f);
  for (int c = 0; c < 2; c++) { svf_set(&R->hlp[c], hlp, 0.7f); svf_set(&R->hlp2[c], hlp, 0.7f); }

  // The shower comes and goes by +-3 dB on its own slow clock.
  R->hiss_g = db2lin(-16.0f + 3.0f * drift_step(&R->shower_dr, dt));
  R->pan = 0.6f * drift_step(&R->pan_dr, dt);
}

// ------------------------------------------------------------ rain (audio)
//
// (a) a Poisson patter of 800-1600 impulses/s through a wandering band-pass and
// (b) a hiss bed 16 dB under it with a slow shower envelope.
// (c) [removed] the occasional droplet chirp - it sounded like bubbles popping.

void env_rain(rain_t *R, float *outL, float *outR) {
  float o[2];
  for (int c = 0; c < 2; c++) {
    float imp = 0.0f;
    if (rng_f(&R->rng[c]) < R->p_hit) imp = rng_pm(&R->rng[c]) * R->hit_amp;
    float pat = svf_lp(&R->lp[c], svf_bp(&R->bp[c], imp));
    float hiss = svf_lp(&R->hlp2[c], svf_lp(&R->hlp[c], svf_hp(&R->hhp[c], rng_pm(&R->rng[c]))))
                 * R->hiss_g;
    o[c] = pat * 2.6f + hiss * 0.5f;
  }
  *outL = o[0];
  *outR = o[1];
}

// ------------------------------------------------- recorded player (audio)
//
// Segment lengths and the crossfade follow the file's own length, so a user who
// drops in their own recording gets sensible scheduling without touching code:
//
//   file < 40 s   segments  6-14 s, crossfade 2.5 s   (fireplace 25.5, wind 14.8)
//   file >= 40 s  segments 20-45 s, crossfade 4.0 s   (stream 145.5, birds 129.8)
//
// Two extra bounds apply. A segment never comes within 1 s of either end of the
// file, and it is capped at span/1.7 so a short file still yields segments that
// can differ from each other — for the 14.8 s wind file that caps segments at
// 7.6 s, which is why wind's range is 6-7.6 s and not 6-14 s. Even so, the
// 30 % overlap rule below is only strictly satisfiable when a segment is under
// span/2.4; for the two short files it is therefore best-effort (measured:
// 115 of 128 handovers in the acceptance runs, worst case 60 %).

// Draw the next segment: a random length in the slot's range, then a random
// start drawn from the positions that keep the overlap with the segment still
// playing at or under 30 % of the new segment's length. The excluded interval
// around the previous start p is asymmetric, because the two segments have
// different lengths: starting later by x overlaps by min(l1 - x, l2) and
// starting earlier by x overlaps by min(l1, l2 - x), so the rule is
// x >= l1 - 0.3*l2 above p and x >= 0.7*l2 below it. The draw is uniform over
// what is left. Constant work, no allocation, no loop.
//
// A short file can make that set empty (a 14.8 s wind file leaves 12.8 s of
// usable span for 6-7.6 s segments, so only starts at the very ends qualify,
// and insisting would turn the scheduler into a two-position ping-pong). In
// that case the draw is uniform over the third of the range furthest from the
// previous segment, which keeps the overlap as low as randomness allows.
static void rec_pick(rec_t *r, rhead_t *h, const rhead_t *prev) {
  int64_t len = r->seg_lo + (int64_t)((float)(r->seg_hi - r->seg_lo) * rng_f(&r->rng));
  int64_t lo = r->span_lo, hi = r->span_hi - len;
  if (hi < lo) hi = lo;
  int64_t start;
  if (prev && prev->on && hi > lo) {
    int64_t p = prev->start;
    int64_t aR = p - (int64_t)(0.7f * (float)len);              // allowed: [lo,aR]
    int64_t bL = p + prev->len - (int64_t)(0.3f * (float)len);  //      and [bL,hi]
    int64_t wA = (aR > lo) ? aR - lo : 0;
    int64_t wB = (hi > bL) ? hi - bL : 0;
    if (wA + wB > 0) {
      int64_t pick = (int64_t)((float)(wA + wB) * rng_f(&r->rng));
      start = (pick < wA) ? lo + pick : bL + (pick - wA);
    } else {
      int64_t third = (hi - lo) / 3;
      start = (p - lo > hi - p) ? lo + (int64_t)((float)third * rng_f(&r->rng))
                                : hi - (int64_t)((float)third * rng_f(&r->rng));
    }
  } else {
    start = lo + (int64_t)((float)(hi - lo) * rng_f(&r->rng));
  }
  if (start < lo) start = lo;
  if (start > hi) start = hi;

  h->on = 1;
  h->t = 0;
  h->start = start;
  h->len = len;
  if (r->log_n < REC_LOG) {
    r->log_start[r->log_n] = (float)start / (float)RATE;
    r->log_len[r->log_n] = (float)len / (float)RATE;
  }
  r->log_n++;
}

void rec_attach(rec_t *r, const wav_t *w) {
  r->w = w;
  r->head[0].on = r->head[1].on = 0;
  r->cur = 0;
  r->log_n = 0;
  if (!w || !w->d) return;

  float span = w->seconds - 2.0f;                 // 1 s clear of each end
  if (span < 1.0f) span = w->seconds;             // pathological short file
  float lo, hi, xf;
  if (w->seconds < 40.0f) { lo = 6.0f;  hi = 14.0f; xf = 2.5f; }
  else                    { lo = 20.0f; hi = 45.0f; xf = 4.0f; }
  float cap = span / 1.7f;                        // the 30 % overlap rule
  if (hi > cap) hi = cap;
  if (hi > span) hi = span;
  if (lo > hi * 0.9f) lo = hi * 0.9f;
  if (xf > lo * 0.45f) xf = lo * 0.45f;           // a segment holds both fades

  r->seg_lo = (int64_t)(lo * (float)RATE);
  r->seg_hi = (int64_t)(hi * (float)RATE);
  r->xf = (int64_t)(xf * (float)RATE);
  if (r->xf < 64) r->xf = 64;
  r->span_lo = (w->seconds > 2.5f) ? RATE : 0;
  r->span_hi = (int64_t)w->frames - r->span_lo;

  // First segment: head 0 fades in from silence over the same crossfade.
  rec_pick(r, &r->head[0], NULL);
}

void rec_control(rec_t *r, float dt, float bright) {
  float lp = clampf(9000.0f * powf(2.0f, bright * 0.7f), 3000.0f, 16000.0f);
  svf_set(&r->lp[0], lp, 0.7f);
  svf_set(&r->lp[1], lp, 0.7f);
  // A very slow +-1.5 dB drift so a long session never feels static.
  r->lev_g = db2lin(1.5f * drift_step(&r->lev_dr, dt));
}

void rec_audio(rec_t *r, float *outL, float *outR) {
  const wav_t *w = r->w;
  float oL = 0.0f, oR = 0.0f;
  if (!w || !w->d) { *outL = 0.0f; *outR = 0.0f; return; }

  for (int i = 0; i < 2; i++) {
    rhead_t *h = &r->head[i];
    if (!h->on) continue;
    // Equal-power (sin/cos) window: g_in^2 + g_out^2 == 1 across the overlap.
    float g;
    if (h->t < r->xf)               g = fsin(0.25f * (float)h->t / (float)r->xf);
    else if (h->t > h->len - r->xf) g = fsin(0.25f * (float)(h->len - h->t) / (float)r->xf);
    else                            g = 1.0f;
    int64_t p = h->start + h->t;
    if (p >= (int64_t)w->frames) p = (int64_t)w->frames - 1;
    const int16_t *fr = w->d + p * (int64_t)w->ch;
    float l = (float)fr[0] * (1.0f / 32768.0f);
    float rr = (w->ch == 1) ? l : (float)fr[1] * (1.0f / 32768.0f);
    oL += l * g;
    oR += rr * g;
    if (++h->t >= h->len) h->on = 0;
  }

  // Hand over: when the leading head has exactly one crossfade left, the other
  // head starts a new random segment and the two cross-fade.
  rhead_t *lead = &r->head[r->cur];
  if (!lead->on || lead->t >= lead->len - r->xf) {
    rhead_t *next = &r->head[r->cur ^ 1];
    if (!next->on) {
      rec_pick(r, next, lead);
      r->cur ^= 1;
    }
  }

  oL = svf_lp(&r->lp[0], svf_hp(&r->hp[0], oL)) * r->lev_g;
  oR = svf_lp(&r->lp[1], svf_hp(&r->hp[1], oR)) * r->lev_g;
  *outL = oL;
  *outR = oR;
}
