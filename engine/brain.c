// Omanoise v3 — musical brain.
#include "brain.h"
#include "sf2.h"

#include <string.h>

// -------------------------------------------------------------- scales
// Major pentatonic only. v2's minor pentatonic went with the mode the user
// named as the anxious one; PLAN v3 §1.5 keeps the roots and drops the minor.

static const int PENT_MAJ[5] = { 0, 2, 4, 7, 9 };   // C D E G A

// -------------------------------------------------------- mode profiles
// dB values are relative to the pad bus (0 dB).

const omode_t MODES[M_COUNT] = {
  [M_FOCUS] = {
    .name = "focus", .lufs_target = -21.0f, .mix_db = -9.0f,
    .macro_lo = 0.70f, .macro_hi = 1.18f, .drift_depth = 0.85f, .lufs_bias = -0.5f,
    .root_midi = 48.0f,                                  // C3
    .scale = PENT_MAJ, .scale_n = 5,
    .pad_warm = 1.00f, .pad_glass = 0.12f, .pad_cut = 2.5f, .pad_db = -8.0f, .bass_db = -5.0f,
    .chord_min = 60.0f, .chord_max = 90.0f,
    .inst = { { SF2_PIANO, 0.68f, 48, 72, 1.00f },
              { SF2_MALLET, 0.32f, 48, 78, 0.80f },
              { -1, 0.0f, 0, 0, 0.0f } },
    .gap_lo = 3.0f, .gap_hi = 8.0f, .phrase_lo = 3, .phrase_hi = 6,
    .rest_lo = 10.0f, .rest_hi = 20.0f, .vel_lo = 34, .vel_hi = 62, .dyad_p = 0.12f,
    .melody_db = 30.0f,
    .pulse_bpm = 62.0f, .kick_db = -22.0f, .tick_db = -30.0f,
    .plate_decay = 0.84f, .plate_damp = 3500.0f, .plate_db = -10.0f,
    .noise_color = 0.40f, .noise_db = -10.0f, .noise_lp = 2600.0f, .nature_boost = 7.0f,
    .ocean_db = -99.0f, .tonal_tilt = -11.0f,
    .am_allowed = 1, .binaural_hz = 12.0f,
  },
  // v4: Relax absorbs Sleep. Every value below is either the v3 Relax value
  // moved about halfway toward v3 Sleep (PLAN v4 §1.2) or, for the noise bed,
  // the exact midpoint of the two — dB averaged in dB, Hz geometrically:
  //   colour  0.55 / 0.85 -> 0.70      noise  -14 / -11 dB -> -12.5 dB
  //   LP      2800 / 2400 -> 2592 Hz   boost    6 / 11 dB ->   8.5 dB
  //   ocean    -12 / -10  -> -11.0 dB
  [M_RELAX] = {
    .name = "relax", .lufs_target = -25.0f, .mix_db = -12.6f,
    .macro_lo = 0.75f, .macro_hi = 1.13f, .drift_depth = 0.68f, .lufs_bias = -0.15f,
    .root_midi = 45.0f,                                  // A2, A major pentatonic
    .scale = PENT_MAJ, .scale_n = 5,
    .pad_warm = 0.45f, .pad_glass = 0.55f, .pad_cut = 1.9f, .pad_db = -3.5f, .bass_db = -4.0f,
    .chord_min = 95.0f, .chord_max = 150.0f,
    .inst = { { SF2_HARP, 0.55f, 48, 72, 1.00f },
              { SF2_PIANO, 0.35f, 36, 60, 1.00f },
              { SF2_BOX, 0.10f, 72, 84, 0.70f } },
    .gap_lo = 7.0f, .gap_hi = 16.0f, .phrase_lo = 1, .phrase_hi = 2,
    .rest_lo = 0.0f, .rest_hi = 0.0f, .vel_lo = 26, .vel_hi = 48, .dyad_p = 0.25f,
    .melody_db = 32.0f,
    .pulse_bpm = 0.0f, .kick_db = -99.0f, .tick_db = -99.0f,
    .plate_decay = 0.90f, .plate_damp = 2750.0f, .plate_db = -8.0f,
    .noise_color = 0.70f, .noise_db = -12.5f, .noise_lp = 2592.0f, .nature_boost = 8.5f,
    .ocean_db = -11.0f, .tonal_tilt = -11.0f,
    .am_allowed = 0, .binaural_hz = 8.0f,
  },
  // v4: the Environment category. The tonal material is a faint background —
  // the bed is the four blendable environment generators, whose levels live in
  // ctl.env and whose gains are computed in layers.c, not here. `ocean_db` is
  // therefore off: the ocean engine is driven by env[ENV_OCEAN] instead.
  [M_ENV] = {
    .name = "environment", .lufs_target = -23.0f, .mix_db = -4.0f,
    .macro_lo = 0.56f, .macro_hi = 1.34f, .drift_depth = 0.85f, .lufs_bias = -0.5f,
    .root_midi = 48.0f,                                  // C3
    .scale = PENT_MAJ, .scale_n = 5,
    .pad_warm = 1.00f, .pad_glass = 0.15f, .pad_cut = 2.0f, .pad_db = -18.0f, .bass_db = -20.0f,
    .chord_min = 120.0f, .chord_max = 160.0f,
    .inst = { { -1, 0.0f, 0, 0, 0.0f }, { -1, 0.0f, 0, 0, 0.0f }, { -1, 0.0f, 0, 0, 0.0f } },
    .gap_lo = 0.0f, .gap_hi = 0.0f, .phrase_lo = 0, .phrase_hi = 0,
    .rest_lo = 0.0f, .rest_hi = 0.0f, .vel_lo = 0, .vel_hi = 0, .dyad_p = 0.0f,
    .melody_db = -99.0f,
    .pulse_bpm = 0.0f, .kick_db = -99.0f, .tick_db = -99.0f,
    .plate_decay = 0.88f, .plate_damp = 2600.0f, .plate_db = -10.0f,
    .noise_color = 0.1f, .noise_db = -99.0f, .noise_lp = 900.0f, .nature_boost = 0.0f,
    .ocean_db = -99.0f, .tonal_tilt = 0.0f,
    .am_allowed = 0, .binaural_hz = 0.0f,
  },
};

const char *mode_name(int m) { return (m >= 0 && m < M_COUNT) ? MODES[m].name : "focus"; }

// v3 names stay accepted for ever: `sleep` is now Relax and `ocean` is now the
// Environment category (PLAN v4 §1). `env` is the short form.
static const struct { const char *from; int to; } MODE_ALIAS[] = {
  { "sleep", M_RELAX }, { "ocean", M_ENV }, { "env", M_ENV },
};

int mode_from_name(const char *s) {
  if (!s) return -1;
  for (int i = 0; i < M_COUNT; i++) if (strcmp(s, MODES[i].name) == 0) return i;
  for (unsigned i = 0; i < sizeof MODE_ALIAS / sizeof MODE_ALIAS[0]; i++)
    if (strcmp(s, MODE_ALIAS[i].from) == 0) return MODE_ALIAS[i].to;
  return -1;
}

// --------------------------------------------------------- chord pools
// Offsets are semitones above the key root; the pool moves by a Markov table
// biased toward transitions that keep at least two common tones. Every chord
// is a wide, consonant add9/6/sus voicing — no thirds stacked in the low
// register, no minor-key pool any more (PLAN v3 §1.5).

static const chord_t POOL_MAJOR[CHORD_POOL] = {
  { 0, { 0, 7, 14, 4 } },    // Imaj9 (no 3rd down low): C G D E
  { 9, { 9, 16, 12, 2 } },   // vi add9: A E C D
  { 5, { 5, 12, 16, 7 } },   // IVmaj7add9: F C E G
  { 7, { 7, 14, 16, 9 } },   // V6/9: G D E A
};
// (v3's Sleep-only sus/11 pool went with the Sleep mode.)

const chord_t *mode_chord(int mode, int idx) {
  (void)mode;
  return &POOL_MAJOR[((idx % CHORD_POOL) + CHORD_POOL) % CHORD_POOL];
}

static int common_tones(const chord_t *a, const chord_t *b) {
  int n = 0;
  for (int i = 0; i < CHORD_TONES; i++)
    for (int j = 0; j < CHORD_TONES; j++)
      if (((a->tone[i] - b->tone[j]) % 12 + 12) % 12 == 0) { n++; break; }
  return n;
}

int chord_next(int mode, int cur, rng_t *rng) {
  float w[CHORD_POOL];
  float sum = 0.0f;
  const chord_t *a = mode_chord(mode, cur);
  for (int i = 0; i < CHORD_POOL; i++) {
    if (i == cur) { w[i] = 0.0f; continue; }
    int ct = common_tones(a, mode_chord(mode, i));
    w[i] = (ct >= 2) ? 3.0f : 0.6f;
    sum += w[i];
  }
  float r = rng_f(rng) * sum;
  for (int i = 0; i < CHORD_POOL; i++) {
    r -= w[i];
    if (r <= 0.0f && w[i] > 0.0f) return i;
  }
  return (cur + 1) % CHORD_POOL;
}

// --------------------------------------------------------- voice leading

// A candidate clashes if it forms a minor 2nd, major 7th or tritone with an
// already sounding voice that is less than 14 semitones away.
//
// v3 adds a low-register rule. Below ~250 Hz the auditory critical band is
// roughly 100 Hz wide, so two pad tones a fourth or a fifth apart down there
// differ by 40-70 Hz — inside their shared critical band, which is precisely
// Plomp & Levelt's sensory-dissonance maximum, and it showed up in the
// roughness metric as a recurring ~49 Hz line (G3 against D3). Nothing closer
// than an octave is allowed when both voices are in the bottom register, which
// is also what "wide consonant voicings" in PLAN v3 §1.5 asks for.
#define LOW_REGISTER_HZ 250.0f

static bool clashes(float hz, const float *avoid, int navoid) {
  for (int i = 0; i < navoid; i++) {
    if (avoid[i] <= 0.0f) continue;
    float st = fabsf(12.0f * log2f(hz / avoid[i]));
    if (hz < LOW_REGISTER_HZ && avoid[i] < LOW_REGISTER_HZ && st < 11.5f) return true;
    if (st >= 14.0f) continue;
    float m = fmodf(st, 12.0f);
    if (m < 1.6f && st > 0.4f) return true;          // minor 2nd
    if (m > 10.4f) return true;                      // major 7th
    if (m > 5.4f && m < 6.6f) return true;           // tritone
  }
  return false;
}

// Equal temperament detunes every interval from its just ratio by up to 16
// cents, and where two chord tones have a nearly coincident harmonic (a fifth
// puts 3*f1 next to 2*f2, a major third 5*f1 next to 4*f2) that mistuning
// comes out as a 1-8 Hz beat — exactly the fluctuation band the v3 acceptance
// metric measures, and exactly the "beating detune = sensory dissonance" that
// research-opensource says to avoid. The sustaining voices are therefore
// snapped to just ratios above the key root, which makes those coincidences
// exact and their beat frequency zero. PLAN v3 §2.1 allows the nudge; here it
// is applied to the pad and bass rather than to the sampled instruments, whose
// notes are short, sparse and never sustained against each other.
static const float JI[12] = {
  1.0f, 16.0f / 15.0f, 9.0f / 8.0f, 6.0f / 5.0f, 5.0f / 4.0f, 4.0f / 3.0f,
  45.0f / 32.0f, 3.0f / 2.0f, 8.0f / 5.0f, 5.0f / 3.0f, 9.0f / 5.0f, 15.0f / 8.0f
};

float ji_snap(float hz, float root_hz) {
  if (hz <= 0.0f || root_hz <= 0.0f) return hz;
  float r = hz / root_hz;
  float lo = log2f(r);
  int oct = (int)floorf(lo);
  float m = r / powf(2.0f, (float)oct);              // in [1,2)
  int best = 0;
  float bd = 1e9f;
  for (int i = 0; i < 12; i++) {
    float d = fabsf(log2f(m / JI[i]));
    if (d < bd) { bd = d; best = i; }
  }
  return root_hz * JI[best] * powf(2.0f, (float)oct);
}

float chord_pick(const chord_t *c, float root_hz, float prev_hz, float center_hz,
                 float lo, float hi, const float *avoid, int navoid, rng_t *rng) {
  float best = 0.0f, best_score = 1e9f;
  float fallback = 0.0f, fallback_score = 1e9f;
  for (int i = 0; i < CHORD_TONES; i++) {
    for (int oct = -3; oct <= 4; oct++) {
      float hz = root_hz * powf(2.0f, ((float)c->tone[i] + 12.0f * (float)oct) / 12.0f);
      if (hz < lo || hz > hi) continue;
      float score = fabsf(log2f(hz / center_hz)) * 1.4f;
      if (prev_hz > 0.0f) score += fabsf(12.0f * log2f(hz / prev_hz)) * 0.30f;
      score += rng_f(rng) * 0.25f;                   // break ties differently each time
      if (score < fallback_score) { fallback_score = score; fallback = hz; }
      if (clashes(hz, avoid, navoid)) continue;
      if (score < best_score) { best_score = score; best = hz; }
    }
  }
  if (best > 0.0f) return ji_snap(best, root_hz);
  // Everything clashed: hold the previous pitch if it is usable, else fall back.
  if (prev_hz > 0.0f && !clashes(prev_hz, avoid, navoid)) return prev_hz;
  return ji_snap(fallback, root_hz);
}

float chord_bass_hz(const chord_t *c, float root_hz, rng_t *rng) {
  int semis = c->bass + (rng_f(rng) < 0.25f ? 7 : 0);   // root, sometimes the fifth
  float hz = root_hz * powf(2.0f, (float)semis / 12.0f);
  while (hz > 110.0f) hz *= 0.5f;
  while (hz < 55.0f)  hz *= 2.0f;
  return ji_snap(hz, root_hz);
}

// ---------------------------------------------------------- melody

// Allowed-next weights by scale-degree step. Steps are strongly favoured and
// leaps discouraged: "smooth melodic shape" is one of the few melodic
// properties relaxation actually correlates with (Grocke & Wigram).
static const float STEP_W[9] = {
// -4    -3    -2    -1     0    +1    +2    +3    +4
   0.15f, 0.4f, 1.3f, 3.2f, 0.6f, 3.2f, 1.3f, 0.5f, 0.2f
};

int melody_next_deg(int mode, int deg, int *repeat_run, rng_t *rng) {
  (void)mode;
  float w[9];
  float sum = 0.0f;
  for (int i = 0; i < 9; i++) {
    w[i] = STEP_W[i];
    if (i == 4 && repeat_run && *repeat_run >= 1) w[i] = 0.0f;   // never 3 in a row
    // Keep the line from wandering out of its register.
    int d = deg + (i - 4);
    if (d < -3) w[i] *= (i < 4) ? 0.05f : 1.6f;
    if (d > 11) w[i] *= (i > 4) ? 0.05f : 1.6f;
    sum += w[i];
  }
  float r = rng_f(rng) * sum;
  int pick = 4;
  for (int i = 0; i < 9; i++) { r -= w[i]; if (r <= 0.0f) { pick = i; break; } }
  int step = pick - 4;
  if (repeat_run) *repeat_run = (step == 0) ? *repeat_run + 1 : 0;
  return deg + step;
}

float degree_hz(int mode, int deg, float root_hz) {
  const omode_t *m = &MODES[mode];
  int n = m->scale_n;
  int oct = (int)floorf((float)deg / (float)n);
  int idx = deg - oct * n;
  if (idx < 0) idx += n;
  if (idx >= n) idx -= n;
  return root_hz * powf(2.0f, ((float)m->scale[idx] + 12.0f * (float)oct) / 12.0f);
}
