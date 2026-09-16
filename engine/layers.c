// Omanoise v4 — layers, buses and the realtime render loop.
//
// The realtime thread never allocates, locks or prints. All parameter work
// happens once per BLOCK (64 samples) and every gain is slewed.
//
// What v2 had here and v3 does not: the supersaw pad voices and their tanh
// stage, the Solina chorus, the shimmer bus, the ping-pong delay, the granular
// cloud and the Risset/FM/Karplus one-shot melody. See docs/PLAN-sound-v3.md.
// v4 removes the Sleep mode and its 20-minute phasing timeline, and adds the
// Environment bus: ocean (the v3 engine below) plus rain, fire and wind from
// env.c, blended by four user levels. See docs/PLAN-sound-v4.md.
#include "layers.h"
#include "brain.h"
#include "control.h"
#include "dsp.h"
#include "env.h"
#include "fx.h"
#include "master.h"

#include <stdatomic.h>

struct control ctl = {
  .playing = 0, .mode = M_FOCUS, .binaural = 0, .adaptive = 1, .pulse = 0, .modulation = 0,
  .volume = 0.7f, .intensity = 0.5f, .brightness = 0.5f, .tonal = 0.5f,
  .env = { 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
  .sounds = { 1, 1, 0, 0, 0, 0 },
  .adapt_bright = 0.0f, .adapt_root = 0.0f,
  .meter = 0.0f, .lufs = -70.0f, .idle = 1, .bank_ready = 0, .sf2_ready = 0,
  .melody_events = 0,
};

// Per-generator calibration: the level that puts each environment sound, alone
// at level 1.0, on the mode's -23 LUFS target before the auto-trim. The two
// synthesized generators were measured with
// `--render environment 120 … --env <name>=1 --stats` (ACCEPTANCE v4); the four
// recorded ones are calibrated at load time from the file's own BS.1770
// loudness (`wav.h:REC_TARGET_LUFS/REC_OFFSET_DB`), so a user-supplied file is
// levelled automatically. The entries below are only used for slots that are
// not being driven by a recording.
static const float ENV_CAL_DB[ENV_N] = {
   -4.4f,      // ocean   (synth)
   +7.5f,      // rain    (synth)
    0.0f,      // fire    (recorded)
    0.0f,      // wind    (recorded)
    0.0f,      // stream  (recorded)
    0.0f,      // birds   (recorded)
};
// The environment bus feeds the plate at -14 dB (PLAN v4 §1.3), except stream
// and birds at -18 dB: those recordings already carry their own room (PLAN v5
// §2.4).
static const float ENV_PLATE_SEND[ENV_N] = {
  0.19953f, 0.19953f, 0.19953f, 0.19953f, 0.12589f, 0.12589f,
};

// ------------------------------------------------------ sample reading

// 4-point Hermite (Catmull-Rom). The bank is band-limited, so this is well
// clear of audible interpolation noise at the resampling ratios we use.
static inline float smp_at(const sample_t *s, double p) {
  int i = (int)p;
  if (i < 1 || i >= s->n - 3) return 0.0f;
  float t = (float)(p - (double)i);
  float y0 = s->d[i - 1], y1 = s->d[i], y2 = s->d[i + 1], y3 = s->d[i + 2];
  float c1 = 0.5f * (y2 - y0);
  float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
  float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
  return ((c3 * t + c2) * t + c1) * t + y1;
}

static inline double smp_wrap(const sample_t *s, double p) {
  if (s->loop1 > s->loop0) {
    double lo = (double)s->loop0, len = (double)(s->loop1 - s->loop0);
    if (p >= (double)s->loop1) {
      p = lo + fmod(p - lo, len);
      if (p < lo) p += len;
    }
  }
  return p;
}

// ----------------------------------------------------------- pad voice
// Each voice reads both PADsynth profiles; the left channel reads table 0 and
// the right table 1. The two tables have the same spectrum and different
// random phases, so the pair is wide in stereo and identical in pitch — there
// is nothing detuned anywhere and therefore nothing beating (PLAN v3 §1.4).

#define NPAD 4

typedef struct {
  double pw_l, pw_r, pg_l, pg_r;   // read positions, warm/glass x L/R
  float  rate_l, rate_r;           // playback ratios (binaural detune lives here)
  float  hz;
  float  env, gain_w, gain_g;
  float  period, t;                // Eno clock
  float  panL, panR, pan;
  svf_t  lpL, lpR;
  drift_t cut_dr, pan_dr;
  float  cut_hz;
  int    silent_cycle;
  rng_t  rng;
} padvoice_t;

// --------------------------------------------------------- pulse voices
// The only one-shots left. The pulse is Focus-only and default off in v3.

#define NSHOT 8

typedef struct {
  const sample_t *s;
  double pos;
  float  ratio;
  float  gL, gR;
  float  atk, atk_step;
  int    active;
  int    filtered;
  svf_t  lp;
} shot_t;

// ---------------------------------------------------- sampled melody notes

#define NNOTE 16

typedef struct { int ch, key; float off_at; int on; } note_t;

// ------------------------------------------------------------- ocean

#define NOCEAN 3

typedef struct {
  float period, t, rise, fall;
  float pan, panL, panR, lev;
  svf_t lp, foam_hp, foam_lp;
  rng_t rng;
  float bl[7];
  float env, fenv, senv;
} ocean_t;

// ------------------------------------------------------------ engine

typedef struct {
  rng_t rng;
  const bank_t *bank;
  sf2_t *sf2;
  int mode, prev_mode;
  float t;                     // seconds of audio rendered
  float play_gain;             // master fade in/out

  float volume, intensity, brightness, tonal;

  int   chord;
  float chord_t, chord_len;
  float root_hz;

  int   mel_deg, mel_repeat, phrase_left;
  float mel_next;
  drift_t mel_pan_dr;

  note_t note[NNOTE];
  int    notes_on;

  padvoice_t pad[NPAD];
  shot_t shot[NSHOT];
  ocean_t ocean[NOCEAN];
  envbank_t envb;              // rain and the four recorded players
  int   env_on;                // this mode drives the environment bus
  float g_env[ENV_N], cur_env[ENV_N];
  const recbank_t *recs;       // published recordings, or NULL

  const sample_t *bass_s;
  double bass_pos;
  float bass_ratio, bass_env, bass_hz, bass_period, bass_t;

  float pulse_t;
  int   pulse_beat, pulse_reset_am;

  rng_t nrngL, nrngR;
  float brownL, brownR;
  float pinkL[7], pinkR[7];
  svf_t nlp1L, nlp2L, nlp1R, nlp2R;
  op_t  ndcL, ndcR;

  plate_t plate;
  svf_t   plate_hp;            // rule 7: the plate never sees anything under 200 Hz
  svf_t   melLP_L, melLP_R;    // darkness control on the sampled instruments
  float   fl_l[BLOCK], fl_r[BLOCK];
  master_t master;

  float duck_env, duck_gain;

  drift_t dr[8];
  float   d[8];
  drift_t macro_dr;
  float   macro;

  float g_pad, g_bass, g_mel, g_noise, g_ocean, g_plate;
  float cur_pad, cur_bass, cur_mel, cur_noise, cur_ocean, cur_plate;
  float g_tonal, cur_tonal;   // tonal-bus scaler, also applied to the plate send
  float mix_g, cur_mix;
  float noise_color;

  float meter;
} engine_t;

static engine_t E;
static int g_layers = LAYER_ALL;
// Published by the bank worker thread, consumed once per control block.
static const bank_t *_Atomic g_bank_ptr = NULL;
static sf2_t *_Atomic g_sf2_ptr = NULL;
static const recbank_t *_Atomic g_rec_ptr = NULL;

void engine_set_layers(int mask) { g_layers = mask & LAYER_ALL; }

// ------------------------------------------------------------ helpers

static float mode_root_hz(int mode) {
  return midi2hz(MODES[mode].root_midi + atomic_load(&ctl.adapt_root));
}

static shot_t *shot_alloc(void) {
  for (int i = 0; i < NSHOT; i++) if (!E.shot[i].active) return &E.shot[i];
  return NULL;
}

static void shot_start(shot_t *sh, const sample_t *s, float gain, float pan,
                       float atk_s, float lp_hz) {
  if (!s || !s->d) return;
  memset(sh, 0, sizeof(*sh));
  sh->s = s;
  sh->pos = 1.0;
  sh->ratio = 1.0f;
  float p = clampf(pan, -1.0f, 1.0f);
  sh->gL = gain * sqrtf(0.5f * (1.0f - p));
  sh->gR = gain * sqrtf(0.5f * (1.0f + p));
  sh->atk = atk_s > 0.0f ? 0.0f : 1.0f;
  sh->atk_step = atk_s > 0.0f ? 1.0f / (atk_s * (float)RATE) : 1.0f;
  if (lp_hz > 0.0f) { svf_reset(&sh->lp); svf_set(&sh->lp, lp_hz, 0.7f); sh->filtered = 1; }
  sh->active = 1;
}

// ---------------------------------------------------------- pad voices

static const float PAD_PERIOD[NPAD] = { 17.3f, 21.9f, 26.1f, 31.7f };
static const float PAD_CENTRE[NPAD] = { 155.0f, 220.0f, 311.0f, 415.0f };

static void pad_repitch(int i) {
  padvoice_t *v = &E.pad[i];
  const chord_t *c = mode_chord(E.mode, E.chord);
  float avoid[NPAD];
  int na = 0;
  for (int k = 0; k < NPAD; k++)
    if (k != i && E.pad[k].env > 0.05f) avoid[na++] = E.pad[k].hz;

  // The register is capped at 460 Hz so the PADsynth tables are never read
  // faster than ~2.1x their recorded rate (PLAN v3 §2.2 asks for 0.5-2).
  float hz = chord_pick(c, E.root_hz, v->hz, PAD_CENTRE[i], 128.0f, 460.0f, avoid, na, &v->rng);
  v->hz = (hz > 0.0f) ? hz : PAD_CENTRE[i];

  const sample_t *w0 = &E.bank->warm[0];
  double lw = (double)(w0->loop1 - w0->loop0);
  v->pw_l = (double)w0->loop0 + rng_f(&v->rng) * lw;
  v->pg_l = (double)w0->loop0 + rng_f(&v->rng) * lw;
  // The right head sits a second or more away in its own table: two kinds of
  // decorrelation at once, and still no comb filtering.
  v->pw_r = smp_wrap(w0, v->pw_l + lw * (0.25 + 0.5 * rng_f(&v->rng)));
  v->pg_r = smp_wrap(w0, v->pg_l + lw * (0.25 + 0.5 * rng_f(&v->rng)));

  // Eno-style dropouts: a voice sometimes sits out a whole cycle, so the
  // texture thins and thickens on its own. At least two voices always stay.
  int others = 0;
  for (int k = 0; k < NPAD; k++)
    if (k != i && !E.pad[k].silent_cycle) others++;
  float p_skip = (E.mode == M_RELAX) ? 0.36f : 0.30f;
  v->silent_cycle = (others >= 2 && rng_f(&v->rng) < p_skip);
}

static void pad_control(float dt, float bright) {
  const omode_t *m = &MODES[E.mode];
  float period_mul = (E.mode == M_RELAX) ? 1.25f : 1.0f;

  float binaural = atomic_load(&ctl.binaural) ? m->binaural_hz : 0.0f;

  for (int i = 0; i < NPAD; i++) {
    padvoice_t *v = &E.pad[i];
    float P = PAD_PERIOD[i] * period_mul;
    v->period = P;
    if (E.bank && v->hz <= 0.0f) { v->t = 0.0f; pad_repitch(i); }   // bank arrived mid-cycle
    v->t += dt;
    if (v->t >= P) {
      v->t -= P;
      if (v->t >= P || v->t < 0.0f) v->t = 0.0f;
      if (E.bank) pad_repitch(i);
    }
    // 4-6 s attack, 8-12 s release, both raised-cosine (PLAN v3 §2.2).
    float A = clampf(P * 0.22f, 4.0f, 6.0f), R = clampf(P * 0.42f, 8.0f, 12.0f);
    if (A + R > P) { float k = P / (A + R); A *= k; R *= k; }
    float env;
    if (v->t < A)          env = 0.5f - 0.5f * cosf(PI_F * v->t / A);
    else if (v->t < P - R) env = 1.0f;
    else                   env = 0.5f + 0.5f * cosf(PI_F * (v->t - (P - R)) / R);
    if (v->silent_cycle) env = 0.0f;
    v->env += (env - v->env) * 0.25f;

    v->gain_w = m->pad_warm;
    v->gain_g = m->pad_glass;

    // Value-noise drift only; the generators run at 0.03-0.08 Hz, two decades
    // below the fluctuation band.
    float cd = drift_step(&v->cut_dr, dt);
    v->cut_hz = clampf(m->pad_cut * v->hz * powf(2.0f, bright + 0.2f * cd), 60.0f, 9000.0f);
    svf_set(&v->lpL, v->cut_hz, 0.6f);
    svf_set(&v->lpR, v->cut_hz, 0.6f);

    float pd = drift_step(&v->pan_dr, dt);
    float pan = clampf(0.45f * pd + ((i & 1) ? 0.22f : -0.22f), -0.8f, 0.8f);
    v->pan += (pan - v->pan) * 0.02f;
    v->panL = sqrtf(0.5f * (1.0f - v->pan));
    v->panR = sqrtf(0.5f * (1.0f + v->pan));

    // Binaural (off by default): +-half the beat frequency between the ears.
    float half = binaural * 0.5f;
    v->rate_l = (v->hz - half) / BANK_PAD_HZ;
    v->rate_r = (v->hz + half) / BANK_PAD_HZ;
  }
}

// ------------------------------------------------------------- melody

static int pick_inst(const omode_t *m, rng_t *rng) {
  float sum = 0.0f;
  for (int i = 0; i < MODE_INST; i++) if (m->inst[i].ch >= 0) sum += m->inst[i].w;
  if (sum <= 0.0f) return -1;
  float r = rng_f(rng) * sum;
  int last = -1;
  for (int i = 0; i < MODE_INST; i++) {
    if (m->inst[i].ch < 0) continue;
    last = i;
    r -= m->inst[i].w;
    if (r <= 0.0f) return i;
  }
  return last;
}

static void note_start(int ch, int key, int vel, float hold) {
  for (int i = 0; i < NNOTE; i++) {
    if (E.note[i].on) continue;
    E.note[i].on = 1;
    E.note[i].ch = ch;
    E.note[i].key = key;
    E.note[i].off_at = E.t + hold;
    sf2_note_on(E.sf2, ch, key, vel);
    E.notes_on++;
    return;
  }
}

static void notes_service(void) {
  for (int i = 0; i < NNOTE; i++) {
    if (!E.note[i].on || E.t < E.note[i].off_at) continue;
    sf2_note_off(E.sf2, E.note[i].ch, E.note[i].key);
    E.note[i].on = 0;
    if (E.notes_on > 0) E.notes_on--;
  }
}

static void notes_flush(void) {
  for (int i = 0; i < NNOTE; i++) E.note[i].on = 0;
  E.notes_on = 0;
  sf2_all_off(E.sf2);
}

static void melody_trigger(void) {
  const omode_t *m = &MODES[E.mode];
  int ii = pick_inst(m, &E.rng);
  if (ii < 0) return;
  const minst_t *in = &m->inst[ii];

  E.mel_deg = melody_next_deg(E.mode, E.mel_deg, &E.mel_repeat, &E.rng);
  float hz = degree_hz(E.mode, E.mel_deg, E.root_hz);
  int key = (int)lroundf(69.0f + 12.0f * log2f(hz / 440.0f));
  while (key < in->lo) key += 12;
  while (key > in->hi) key -= 12;
  if (key < in->lo) key = in->lo;

  // Velocity carries the per-instrument trim: on a sampled instrument that is
  // also the right thing timbrally, because a softer note is a darker note.
  int vel = m->vel_lo + rng_int(&E.rng, m->vel_hi - m->vel_lo + 1);
  vel = (int)(vel * in->gain);
  if (vel < 10) vel = 10;
  if (vel > 100) vel = 100;

  float hold = sf2_channel_hold(in->ch);
  note_start(in->ch, key, vel, hold);

  // Occasional dyad: a fifth, sixth or octave above, never closer than that.
  if (rng_f(&E.rng) < m->dyad_p) {
    static const int IVL[3] = { 7, 9, 12 };
    int k2 = key + IVL[rng_int(&E.rng, 3)];
    if (k2 <= in->hi) note_start(in->ch, k2, (int)(vel * 0.72f) < 10 ? 10 : (int)(vel * 0.72f), hold);
  }

  atomic_fetch_add(&ctl.melody_events, 1);
}

// ------------------------------------------------------------- ocean

static inline float wave_env(float t, float rise, float fall) {
  if (t <= 0.0f || t >= rise + fall) return 0.0f;
  if (t < rise) return 0.5f - 0.5f * cosf(PI_F * t / rise);
  float x = (t - rise) / fall;
  return (1.0f - x) * (1.0f - x);
}

// ------------------------------------------------------------- init

int engine_init(uint64_t seed) {
  memset(&E, 0, sizeof(E));
  dsp_tables_init();
  rng_seed(&E.rng, seed);
  rng_seed(&E.nrngL, seed ^ 0xA5A5A5A5ull);
  rng_seed(&E.nrngR, seed ^ 0x5A5A5A5Aull);

  E.mode = E.prev_mode = atomic_load(&ctl.mode);
  E.volume = atomic_load(&ctl.volume);
  E.intensity = atomic_load(&ctl.intensity);
  E.brightness = atomic_load(&ctl.brightness);
  E.tonal = atomic_load(&ctl.tonal);
  E.root_hz = mode_root_hz(E.mode);
  E.chord_len = MODES[E.mode].chord_min;
  E.mel_deg = 4;
  E.mel_next = 8.0f;
  E.phrase_left = 0;
  E.bass_period = 37.4f;
  E.bass_t = E.bass_period - 1.0f;      // bass enters almost immediately
  E.duck_gain = 1.0f;
  E.mix_g = E.cur_mix = db2lin(MODES[E.mode].mix_db);
  E.g_tonal = E.cur_tonal = 1.0f;
  E.noise_color = MODES[E.mode].noise_color;
  drift_init(&E.mel_pan_dr, seed + 909u, 40.0f);

  for (int i = 0; i < NPAD; i++) {
    padvoice_t *v = &E.pad[i];
    rng_seed(&v->rng, seed + 0x9E37u * (uint64_t)(i + 1));
    v->period = PAD_PERIOD[i];
    v->t = PAD_PERIOD[i] - 0.001f - (float)i * 1.3f;   // staggered entries
    if (v->t < 0.0f) v->t = 0.0f;
    v->hz = 0.0f;                                     // repitched when the bank lands
    v->rate_l = v->rate_r = 1.0f;
    drift_init(&v->cut_dr, seed + 11u * (uint64_t)(i + 1), 17.0f + 5.3f * (float)i);
    drift_init(&v->pan_dr, seed + 71u * (uint64_t)(i + 1), 29.0f + 7.1f * (float)i);
    svf_set(&v->lpL, 600.0f, 0.6f);
    svf_set(&v->lpR, 600.0f, 0.6f);
    v->panL = v->panR = 0.707f;
  }
  for (int i = 0; i < 8; i++)
    drift_init(&E.dr[i], seed + 1013u * (uint64_t)(i + 1), 11.0f + 4.1f * (float)i);
  drift_init(&E.macro_dr, seed + 60013u, 26.0f);
  drift_set_alternating(&E.macro_dr, 1);

  static const float OC_PERIOD[NOCEAN] = { 8.6f, 11.4f, 14.2f };
  static const float OC_PAN[NOCEAN]    = { -0.7f, 0.1f, 0.75f };
  for (int i = 0; i < NOCEAN; i++) {
    ocean_t *o = &E.ocean[i];
    rng_seed(&o->rng, seed + 4099u * (uint64_t)(i + 1));
    o->period = OC_PERIOD[i];
    o->t = rng_f(&o->rng) * o->period;
    o->rise = o->period * 0.44f;
    o->fall = o->period * 0.54f;
    o->pan = OC_PAN[i];
    o->panL = sqrtf(0.5f * (1.0f - o->pan));
    o->panR = sqrtf(0.5f * (1.0f + o->pan));
    o->lev = 1.0f / (1.0f + 0.35f * (float)i);   // one dominant wave, two behind it
    svf_set(&o->lp, 120.0f, 0.8f);
    svf_set(&o->foam_hp, 1500.0f, 0.7f);
    svf_set(&o->foam_lp, 2900.0f, 0.7f);
  }

  env_init(&E.envb, seed ^ 0x0E4D1A11ull);

  svf_set(&E.nlp1L, 1200.0f, 0.7f); svf_set(&E.nlp2L, 1200.0f, 0.7f);
  svf_set(&E.nlp1R, 1200.0f, 0.7f); svf_set(&E.nlp2R, 1200.0f, 0.7f);
  op_set(&E.ndcL, 18.0f); op_set(&E.ndcR, 18.0f);

  if (plate_init(&E.plate, seed ^ 0xC0FFEEull)) return -1;
  svf_set(&E.plate_hp, 200.0f, 0.7f);
  svf_set(&E.melLP_L, 3000.0f, 0.7f);
  svf_set(&E.melLP_R, 3000.0f, 0.7f);
  if (master_init(&E.master)) return -1;
  return 0;
}

void engine_free(void) {
  plate_free(&E.plate);
  master_free(&E.master);
}

float engine_trim_db(void) { return E.master.trim_db; }

void engine_set_bank(const bank_t *b) {
  atomic_store_explicit(&g_bank_ptr, b, memory_order_release);
  atomic_store(&ctl.bank_ready, 1);
}

void engine_set_sf2(sf2_t *s) {
  atomic_store_explicit(&g_sf2_ptr, s, memory_order_release);
  atomic_store(&ctl.sf2_ready, s ? 1 : 0);
}

// One atomic pointer swap publishes every loaded recording at once; the audio
// thread picks it up on its next control block and attaches each player.
void engine_set_sounds(const recbank_t *r) {
  atomic_store_explicit(&g_rec_ptr, r, memory_order_release);
}

int engine_seg_log(int slot, int i, float *start_s, float *len_s) {
  if (slot < 0 || slot >= ENV_N) return 0;
  const rec_t *r = &E.envb.rec[slot];
  int have = r->log_n < REC_LOG ? r->log_n : REC_LOG;
  if (i < 0) return have;
  if (i >= have) return 0;
  if (start_s) *start_s = r->log_start[i];
  if (len_s) *len_s = r->log_len[i];
  return 1;
}

// -------------------------------------------------------- control rate

static void engine_control(void) {
  const float dt = BLOCK_DT;
  E.t += dt;
  E.bank = atomic_load_explicit(&g_bank_ptr, memory_order_acquire);
  E.sf2  = atomic_load_explicit(&g_sf2_ptr, memory_order_acquire);

  slew(&E.volume, atomic_load(&ctl.volume), 0.02f);
  slew(&E.intensity, atomic_load(&ctl.intensity), 0.02f);
  slew(&E.brightness, atomic_load(&ctl.brightness), 0.02f);
  slew(&E.tonal, atomic_load(&ctl.tonal), 0.02f);

  int mode = atomic_load(&ctl.mode);
  if (mode != E.mode) {
    E.prev_mode = E.mode;
    E.mode = mode;
    E.chord_t = 1e9f;
    E.bass_t = E.bass_period - 1.0f;
    E.mel_next = 6.0f;
    E.phrase_left = 0;
    notes_flush();
    // Push every pad voice into its release so the new key arrives within
    // ~10 s instead of waiting out a 30 s Eno cycle.
    for (int i = 0; i < NPAD; i++) {
      padvoice_t *v = &E.pad[i];
      float R = clampf(v->period * 0.42f, 8.0f, 12.0f);
      if (v->t < v->period - R) v->t = v->period - R;
    }
  }
  const omode_t *m = &MODES[E.mode];
  slew(&E.root_hz, mode_root_hz(E.mode), 0.004f);
  slew(&E.noise_color, m->noise_color, 0.004f);

  bool playing = atomic_load(&ctl.playing);
  slew(&E.play_gain, playing ? 1.0f : 0.0f, playing ? 0.0011f : 0.0022f);
  if (!playing && E.play_gain < 0.002f) { E.play_gain = 0.0f; atomic_store(&ctl.idle, 1); }
  else atomic_store(&ctl.idle, 0);

  for (int i = 0; i < 8; i++) E.d[i] = drift_step(&E.dr[i], dt);
  // One shared contour that moves every layer together between a low and a
  // high profile (myNoise "Animate"): this is what gives the mix its LRA.
  E.macro = lerpf(m->macro_lo, m->macro_hi, 0.5f + 0.5f * drift_step(&E.macro_dr, dt));

  // ---- harmony clock
  E.chord_t += dt;
  if (E.chord_t >= E.chord_len) {
    E.chord_t = 0.0f;
    E.chord = chord_next(E.mode, E.chord, &E.rng);
    E.chord_len = rng_range(&E.rng, m->chord_min, m->chord_max);
  }

  // ---- tonal crossfade, referenced to tonal = 0.5
  float tonal_db  = (E.tonal >= 0.5f) ? lerpf(0.0f, 3.0f, (E.tonal - 0.5f) * 2.0f)
                                      : lerpf(-21.0f, 0.0f, E.tonal * 2.0f);
  // The nature layers rise as the tonal layers fall. The boost is large
  // because at tonal 0 the bed alone has to carry the mode's loudness target
  // (minus the 11 LU "pure noise generator" tilt below), and the master trim
  // only has +-6 dB of authority.
  float noise_tdb = (E.tonal >= 0.5f) ? lerpf(0.0f, -3.0f, (E.tonal - 0.5f) * 2.0f)
                                      : lerpf(m->nature_boost, 0.0f, E.tonal * 2.0f);
  float tonal_g = (E.tonal < 0.02f) ? 0.0f : db2lin(tonal_db) * E.macro;

  float inten = 0.4f + 1.2f * E.intensity;          // 0.4 .. 1.6, PLAN v3 §2.3
  float bright = (E.brightness - 0.5f) * 2.0f + atomic_load(&ctl.adapt_bright);

  // ---- layer gains; the drifts move each one independently (myNoise Animate)
  const float dd = m->drift_depth;
  #define DRIFTG(lo, hi, dv) (1.0f + (lerpf((lo), (hi), 0.5f + 0.5f * (dv)) - 1.0f) * dd)
  const int core = (g_layers & LAYER_CORE) ? 1 : 0;
  E.g_pad   = core ? db2lin(m->pad_db)  * tonal_g * DRIFTG(0.65f, 1.22f, E.d[0]) : 0.0f;
  E.g_bass  = core ? db2lin(m->bass_db) * tonal_g : 0.0f;
  E.g_mel   = (m->melody_db < -90.0f || !(g_layers & LAYER_EVENTS))
              ? 0.0f : db2lin(m->melody_db) * tonal_g;
  E.g_noise = (core && m->noise_db > -90.0f)
              ? db2lin(m->noise_db + noise_tdb + (E.intensity - 0.5f) * 8.0f)
                * DRIFTG(0.72f, 1.22f, E.d[2]) * E.macro : 0.0f;
  if (!(g_layers & LAYER_NATURE) || m->ocean_db < -90.0f) E.g_ocean = 0.0f;
  else E.g_ocean = db2lin(m->ocean_db + noise_tdb) * E.macro;
  E.g_plate = db2lin(m->plate_db) * DRIFTG(0.85f, 1.18f, E.d[7]);
  #undef DRIFTG

  // ---- environment bank (Environment mode only)
  //
  // Levels are perceptual: gain goes as level^2, and each generator is
  // pre-calibrated so that level 1.0 alone lands on the mode's loudness target.
  // The four are then summed through a power-preserving soft knee, so blending
  // sounds does not stack their power: four at 0.7 carry the same total energy
  // as one at 1.0, and nothing below a total of 1.0 is touched at all.
  E.env_on = (E.mode == M_ENV) && (g_layers & LAYER_NATURE);
  {
    // Pick up the recordings the loader published and (re)attach any player
    // whose sample changed. This happens once, on the block after the swap.
    E.recs = atomic_load_explicit(&g_rec_ptr, memory_order_acquire);
    for (int i = 0; i < ENV_N; i++) {
      const wav_t *w = (E.recs && E.recs->w[i].d) ? &E.recs->w[i] : NULL;
      if (E.envb.rec[i].w != w) rec_attach(&E.envb.rec[i], w);
    }

    float lev[ENV_N], w = 0.0f;
    for (int i = 0; i < ENV_N; i++) {
      // A slot with no generator (a recorded slot whose file is missing) is
      // held at zero, so it neither sounds nor takes part in the soft knee.
      int live = (i == ENV_OCEAN || i == ENV_RAIN) || rec_ready(&E.envb.rec[i]);
      lev[i] = (E.env_on && live) ? clampf(atomic_load(&ctl.env[i]), 0.0f, 1.0f) : 0.0f;
      w += lev[i] * lev[i];
    }
    float bus = 1.0f / sqrtf(fmaxf(1.0f, w));
    for (int i = 0; i < ENV_N; i++) {
      float cal = rec_ready(&E.envb.rec[i]) ? E.envb.rec[i].w->cal_db : ENV_CAL_DB[i];
      E.g_env[i] = lev[i] * lev[i] * db2lin(cal) * bus * E.macro;
    }
    if (E.env_on) E.g_ocean = E.g_env[ENV_OCEAN];
    env_control(&E.envb, dt, bright, E.intensity, lev);
    for (int i = 0; i < ENV_N; i++)
      if (rec_ready(&E.envb.rec[i])) rec_control(&E.envb.rec[i], dt, bright);
  }
  E.mix_g = db2lin(m->mix_db);
  E.g_tonal = tonal_g;

  // ---- noise bed filter
  float nlp = clampf(m->noise_lp * powf(2.0f, bright * 0.7f + 0.25f * E.d[3]), 120.0f, 6000.0f);
  svf_set(&E.nlp1L, nlp, 0.7f); svf_set(&E.nlp2L, nlp, 0.7f);
  svf_set(&E.nlp1R, nlp, 0.7f); svf_set(&E.nlp2R, nlp, 0.7f);

  // ---- plate (one, darker and shorter than v2's pair)
  float damp = clampf(m->plate_damp * powf(2.0f, bright * 0.7f + 0.2f * E.d[4]), 700.0f, 6000.0f);
  plate_set(&E.plate, m->plate_decay, damp, 6000.0f, 40.0f);
  plate_drift(&E.plate, dt);

  // ---- sampled-instrument darkness
  float mel_lp = clampf(3000.0f * powf(2.0f, bright), 800.0f, 9000.0f);
  svf_set(&E.melLP_L, mel_lp, 0.7f);
  svf_set(&E.melLP_R, mel_lp, 0.7f);
  if (E.sf2) sf2_set_dark(E.sf2, clampf(bright * 600.0f, -900.0f, 900.0f));

  pad_control(dt, bright);

  // ---- bass on its own slow clock
  E.bass_t += dt;
  if (E.bass_t >= E.bass_period) {
    E.bass_t -= E.bass_period;
    if (E.bass_t >= E.bass_period || E.bass_t < 0.0f) E.bass_t = 0.0f;
    if (E.bank) {
      E.bass_hz = chord_bass_hz(mode_chord(E.mode, E.chord), E.root_hz, &E.rng);
      E.bass_s = bank_pick(E.bank->bass, BANK_BASS_N, E.bass_hz);
      E.bass_ratio = E.bass_hz / E.bass_s->f0;
      E.bass_pos = (double)E.bass_s->loop0;
    }
  }
  {
    float P = E.bass_period, A = 8.0f, R = 12.0f;
    float env;
    if (E.bass_t < A)          env = 0.5f - 0.5f * cosf(PI_F * E.bass_t / A);
    else if (E.bass_t < P - R) env = 1.0f;
    else                       env = 0.5f + 0.5f * cosf(PI_F * (E.bass_t - (P - R)) / R);
    // A sub that fades to silence and back is a large slow loudness swing; every
    // mode now keeps a floor under it (v2 already did this for Sleep and Ocean).
    env = 0.40f + 0.60f * env;
    E.bass_env += (env - E.bass_env) * 0.25f;
  }

  // ---- melody: phrases of a few notes, then a long rest. No two events are
  // ever closer than 1.5 s, whatever the intensity slider says.
  notes_service();
  if (E.sf2 && E.bank && E.play_gain > 0.05f && (g_layers & LAYER_EVENTS) &&
      m->inst[0].ch >= 0 && E.tonal > 0.05f) {
    E.mel_next -= dt;
    if (E.mel_next <= 0.0f) {
      melody_trigger();
      if (E.phrase_left > 1) {
        E.phrase_left--;
        E.mel_next = rng_range(&E.rng, m->gap_lo, m->gap_hi) / inten;
      } else {
        E.phrase_left = m->phrase_lo + rng_int(&E.rng, m->phrase_hi - m->phrase_lo + 1);
        E.mel_next = (rng_range(&E.rng, m->gap_lo, m->gap_hi)
                      + rng_range(&E.rng, m->rest_lo, m->rest_hi)) / inten;
      }
      if (E.mel_next < 1.5f) E.mel_next = 1.5f;
    }
  } else {
    E.mel_next = 4.0f;
    E.phrase_left = 0;
  }

  // Instrument pan drifts continuously and slowly; one generator, four fixed
  // offsets, so two instruments never sit in the same place.
  if (E.sf2) {
    float d = drift_step(&E.mel_pan_dr, dt);
    sf2_pan(E.sf2, SF2_PIANO,   clampf(0.30f * d, -0.4f, 0.4f));
    sf2_pan(E.sf2, SF2_HARP,    clampf(-0.30f * d, -0.4f, 0.4f));
    sf2_pan(E.sf2, SF2_MALLET, clampf(0.20f * d + 0.25f, -0.5f, 0.5f));
    sf2_pan(E.sf2, SF2_BOX,     clampf(-0.20f * d - 0.25f, -0.5f, 0.5f));
  }

  // ---- pulse (focus only, default off), 8th notes: kick on, tick off the beat
  E.pulse_reset_am = 0;
  if (E.bank && m->pulse_bpm > 0.0f && atomic_load(&ctl.pulse) && (g_layers & LAYER_EVENTS) &&
      E.play_gain > 0.05f && E.tonal > 0.05f) {
    float half_beat = 30.0f / m->pulse_bpm;
    E.pulse_t += dt;
    if (E.pulse_t >= half_beat) {
      E.pulse_t -= half_beat;
      if (E.pulse_t >= half_beat || E.pulse_t < 0.0f) E.pulse_t = 0.0f;
      E.pulse_beat++;
      shot_t *sh = shot_alloc();
      if (sh) {
        if ((E.pulse_beat & 1) == 0) {
          shot_start(sh, &E.bank->kick,
                     db2lin(m->kick_db) * (0.6f + 0.8f * E.intensity) * E.g_tonal, 0.0f, 0.010f, 0.0f);
          E.pulse_reset_am = 1;
        } else {
          shot_start(sh, &E.bank->tick,
                     db2lin(m->tick_db) * (0.6f + 0.8f * E.intensity) * E.g_tonal,
                     rng_range(&E.rng, -0.35f, 0.35f), 0.006f, 5000.0f);
        }
      }
    }
  } else {
    E.pulse_t = 0.0f;
  }

  // ---- ocean engines: rumble then lagging foam, three incommensurate periods
  for (int i = 0; i < NOCEAN; i++) {
    ocean_t *o = &E.ocean[i];
    o->t += dt;
    if (o->t >= o->period) {
      o->t -= o->period;
      if (o->t >= o->period || o->t < 0.0f) o->t = 0.0f;
      // Re-drawn every cycle: the wave train is aperiodic, so nothing here can
      // show up as a line in the fluctuation spectrum.
      o->period = (10.5f + 2.8f * (float)i) * (0.92f + 0.16f * rng_f(&o->rng));
      o->rise = o->period * rng_range(&o->rng, 0.40f, 0.48f);
      o->fall = o->period * rng_range(&o->rng, 0.50f, 0.58f);
    }
    o->env = wave_env(o->t, o->rise, o->fall);
    o->fenv = wave_env(o->t - 0.8f, 1.0f, 2.2f);
    // The cutoff follows a lagged copy of the wave envelope, which keeps the
    // swell while holding the per-second loudness step down.
    o->senv += (o->env - o->senv) * tau_k(1.1f, 1.0f / BLOCK_DT);
    svf_set(&o->lp, clampf((85.0f + 265.0f * o->senv) * powf(2.0f, bright * 0.4f), 30.0f, 900.0f), 0.8f);
  }

  // ---- the sampled instruments, one control block at a time
  int want_fluid = E.sf2 && (g_layers & LAYER_EVENTS) &&
                   (E.notes_on > 0 || sf2_active_voices(E.sf2) > 0);
  if (want_fluid) sf2_render(E.sf2, E.fl_l, E.fl_r, BLOCK);
  else { memset(E.fl_l, 0, sizeof E.fl_l); memset(E.fl_r, 0, sizeof E.fl_r); }

  // ---- master
  float am_depth = 0.0f;
  int am_on = 0;
  if (m->am_allowed && atomic_load(&ctl.modulation) && E.tonal > 0.05f && (g_layers & LAYER_NATURE)) {
    am_on = 1;
    float mins = E.t / 60.0f;
    // Depth is capped at 0.35 in v3: 16 Hz is inside the roughness band, so the
    // toggle stays available but can no longer reach v2's 0.60.
    if (mins < 10.0f) am_depth = lerpf(0.18f, 0.35f, mins / 10.0f);
    else              am_depth = 0.35f;
  }
  // At tonal 0 only the bed is left, and a pure noise generator is asked to be
  // quieter than a full mix — except in Environment, where the bed *is* the
  // mode and the tonal layer is only a faint background (m->tonal_tilt = 0).
  float lufs_tilt = (E.tonal >= 0.5f) ? 0.0f : lerpf(m->tonal_tilt, 0.0f, E.tonal * 2.0f);
  float lp_hz = clampf(9000.0f * powf(2.0f, bright * 0.35f), 4000.0f, 14000.0f);
  master_control(&E.master, m->lufs_target + m->lufs_bias + lufs_tilt, am_on, am_depth,
                 1.3f, lp_hz, E.play_gain > 0.95f);
  if (E.pulse_reset_am) E.master.am_ph = 0.0f;
  atomic_store(&ctl.lufs, E.master.lufs);
}

// ------------------------------------------------------------- render

void engine_render(float *out, uint32_t frames) {
  static int block_pos = 0;
  float sumsq = 0.0f;

  for (uint32_t n = 0; n < frames; n++) {
    if (block_pos == 0) engine_control();
    int idx = block_pos;
    if (++block_pos >= BLOCK) block_pos = 0;

    E.cur_pad   += (E.g_pad   - E.cur_pad)   * 0.002f;
    E.cur_bass  += (E.g_bass  - E.cur_bass)  * 0.002f;
    E.cur_mel   += (E.g_mel   - E.cur_mel)   * 0.002f;
    E.cur_noise += (E.g_noise - E.cur_noise) * 0.002f;
    E.cur_ocean += (E.g_ocean - E.cur_ocean) * 0.002f;
    E.cur_plate += (E.g_plate - E.cur_plate) * 0.002f;
    for (int i = 1; i < ENV_N; i++) E.cur_env[i] += (E.g_env[i] - E.cur_env[i]) * 0.002f;
    E.cur_mix += (E.mix_g - E.cur_mix) * 0.0008f;
    E.cur_tonal += (E.g_tonal - E.cur_tonal) * 0.002f;

    float padL = 0.0f, padR = 0.0f;
    float pulseL = 0.0f, pulseR = 0.0f;
    float plate_in = 0.0f;

    // ------------------------------------------------------------ pads
    const bank_t *bank = E.bank;
    if (bank) {
      const sample_t *w0 = &bank->warm[0], *w1 = &bank->warm[1];
      const sample_t *g0 = &bank->glass[0], *g1 = &bank->glass[1];
      for (int i = 0; i < NPAD; i++) {
        padvoice_t *v = &E.pad[i];
        if (v->env < 0.0005f || v->hz <= 0.0f) continue;
        float l = 0.0f, r = 0.0f;
        if (v->gain_w > 0.001f) {
          l += v->gain_w * smp_at(w0, v->pw_l);
          r += v->gain_w * smp_at(w1, v->pw_r);
        }
        if (v->gain_g > 0.001f) {
          l += v->gain_g * smp_at(g0, v->pg_l);
          r += v->gain_g * smp_at(g1, v->pg_r);
        }
        v->pw_l = smp_wrap(w0, v->pw_l + (double)v->rate_l);
        v->pw_r = smp_wrap(w1, v->pw_r + (double)v->rate_r);
        v->pg_l = smp_wrap(g0, v->pg_l + (double)v->rate_l);
        v->pg_r = smp_wrap(g1, v->pg_r + (double)v->rate_r);
        // No tanh here any more: PADsynth material already has the spectrum we
        // want and a waveshaper would only add harmonics nobody asked for.
        l = svf_lp(&v->lpL, l);
        r = svf_lp(&v->lpR, r);
        padL += l * v->env * v->panL;
        padR += r * v->env * v->panR;
      }
    }

    // ------------------------------------------------------------ bass
    float bass = 0.0f;
    if (bank && E.bass_s && E.bass_env > 0.0005f) {
      bass = smp_at(E.bass_s, E.bass_pos) * E.bass_env;
      E.bass_pos = smp_wrap(E.bass_s, E.bass_pos + (double)E.bass_ratio);
    }

    // ----------------------------------------------------- pulse voices
    for (int i = 0; i < NSHOT; i++) {
      shot_t *sh = &E.shot[i];
      if (!sh->active) continue;
      float x = smp_at(sh->s, sh->pos);
      sh->pos += (double)sh->ratio;
      if (sh->filtered) x = svf_lp(&sh->lp, x);
      if (sh->atk < 1.0f) {
        x *= 0.5f - 0.5f * cosf(PI_F * sh->atk);
        sh->atk += sh->atk_step;
      }
      if ((int)sh->pos >= sh->s->n - 4) sh->active = 0;
      pulseL += x * sh->gL;
      pulseR += x * sh->gR;
    }

    // ------------------------------------------------ sampled instruments
    float mL = svf_lp(&E.melLP_L, E.fl_l[idx]) * E.cur_mel;
    float mR = svf_lp(&E.melLP_R, E.fl_r[idx]) * E.cur_mel;
    float mel_mono = 0.5f * (mL + mR);

    // ------------------------------------------------------- noise bed
    float wl = rng_pm(&E.nrngL), wr = rng_pm(&E.nrngR);
    float pl = pink_step(E.pinkL, wl), pr = pink_step(E.pinkR, wr);
    E.brownL += 0.0016f * (wl - E.brownL);
    E.brownR += 0.0016f * (wr - E.brownR);
    float nl = lerpf(E.brownL * 14.0f, pl * 2.2f, E.noise_color);
    float nr = lerpf(E.brownR * 14.0f, pr * 2.2f, E.noise_color);
    nl = op_hp(&E.ndcL, svf_lp(&E.nlp2L, svf_lp(&E.nlp1L, nl)));
    nr = op_hp(&E.ndcR, svf_lp(&E.nlp2R, svf_lp(&E.nlp1R, nr)));
    float natL = nl * E.cur_noise, natR = nr * E.cur_noise;

    // ------------------------------------------- ocean / environment bank
    // In Environment mode the ocean is one of four blendable sounds and the
    // whole bus goes on to the plate; in Relax it is a quiet bed inside the
    // noise layer, exactly as in v3.
    float envL = 0.0f, envR = 0.0f, envSend = 0.0f;
    // A recorded ocean.wav, if the user drops one in, replaces the synthesized
    // wave engine inside the environment bus (the quiet Relax bed stays synth).
    const int ocean_rec = E.env_on && rec_ready(&E.envb.rec[ENV_OCEAN]);
    if (E.cur_ocean > 1e-5f && !ocean_rec) {
      for (int i = 0; i < NOCEAN; i++) {
        ocean_t *o = &E.ocean[i];
        float w = rng_pm(&o->rng);
        // Distant surf never stops completely: a floor under the wave envelope
        // keeps the bed continuous and stops the short-term loudness swinging
        // like a gate. The filter sweep still follows the raw envelope.
        float ae = 0.62f + 0.38f * o->env;
        float fe = 0.58f + 0.42f * o->fenv;
        float rumble = svf_lp(&o->lp, w) * 3.2f * ae;
        float foam = svf_lp(&o->foam_lp, svf_hp(&o->foam_hp, pink_step(o->bl, w) * 2.2f))
                     * 0.85f * fe;
        float v = (rumble + foam) * E.cur_ocean * o->lev;
        if (E.env_on) {
          envL += v * o->panL; envR += v * o->panR;
          envSend += v * 0.5f * (o->panL + o->panR) * ENV_PLATE_SEND[ENV_OCEAN];
        } else {
          natL += v * o->panL; natR += v * o->panR;
        }
      }
    }
    if (E.env_on) {
      for (int i = 0; i < ENV_N; i++) {
        float lv = (i == ENV_OCEAN) ? E.cur_ocean : E.cur_env[i];
        if (lv <= 1e-5f) continue;
        float l, r;
        if (rec_ready(&E.envb.rec[i])) rec_audio(&E.envb.rec[i], &l, &r);
        else if (i == ENV_RAIN) env_rain(&E.envb.rain, &l, &r);
        else continue;                      // ocean is the block above
        envL += l * lv;
        envR += r * lv;
        envSend += 0.5f * (l + r) * lv * ENV_PLATE_SEND[i];
      }
    }

    // ---------------------------------------------------- pad bus, duck
    float mabs = fabsf(mel_mono);
    E.duck_env += (mabs - E.duck_env) * (mabs > E.duck_env ? 0.01f : 0.00007f);
    float duck = clampf(1.0f / (1.0f + E.duck_env * 2.2f), db2lin(-2.5f), 1.0f);
    E.duck_gain += (duck - E.duck_gain) * 0.002f;

    float padOL = padL * E.cur_pad * E.duck_gain;
    float padOR = padR * E.cur_pad * E.duck_gain;
    plate_in += 0.5f * (padOL + padOR) * 0.22f;
    plate_in += mel_mono * 0.9f;
    plate_in += envSend;

    float bassO = bass * E.cur_bass;

    // ----------------------------------------------------------- plate
    float rvL, rvR;
    plate_run(&E.plate, svf_hp(&E.plate_hp, plate_in), &rvL, &rvR);
    rvL *= E.cur_plate; rvR *= E.cur_plate;

    // ------------------------------------------------------------- sum
    float sumL = padOL + bassO + mL + natL + envL + rvL + pulseL;
    float sumR = padOR + bassO + mR + natR + envR + rvR + pulseR;

    float fade = E.play_gain * E.cur_mix;
    sumL *= fade;
    sumR *= fade;

    float oL, oR;
    master_run(&E.master, sumL, sumR, E.volume, &oL, &oR);
    out[n * CH] = oL;
    out[n * CH + 1] = oR;
    sumsq += oL * oL + oR * oR;
  }

  float rms = sqrtf(sumsq / (float)(frames * CH));
  E.meter = fmaxf(rms, E.meter * 0.85f);
  atomic_store(&ctl.meter, E.meter);
}
