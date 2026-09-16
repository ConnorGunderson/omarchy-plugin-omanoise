// Omanoise v3 — the musical brain: mode profiles, key, chord pool, Markov
// transitions, voice leading and melody generation. Pure decision logic, no
// audio: everything here runs at control rate (every 64 samples) or rarer.
//
// v3 changes: major pentatonic in every mode (no minor), sampled instruments
// instead of bell/pluck "kinds", far sparser and softer events, and no mode
// carries a shimmer or granular bus any more.
#ifndef OMANOISE_BRAIN_H
#define OMANOISE_BRAIN_H

#include "common.h"

// v4: Sleep is merged into Relax and Ocean becomes the Environment category.
enum { M_FOCUS, M_RELAX, M_ENV, M_COUNT };

#define CHORD_TONES 4
#define CHORD_POOL  4
#define MODE_INST   3

typedef struct {
  int bass;                 // semitones above the key root
  int tone[CHORD_TONES];    // pitch classes (may exceed an octave)
} chord_t;

// One sampled instrument slot in a mode's melody palette.
typedef struct {
  int   ch;                 // SF2_* channel, -1 = unused
  float w;                  // selection weight
  int   lo, hi;             // MIDI register for this instrument
  float gain;               // linear trim relative to the melody bus
} minst_t;

typedef struct {
  const char *name;
  float lufs_target;        // integrated LUFS at volume 1.0, tonal 0.5
  float mix_db;             // static pre-master calibration so the trim sits near 0
  float macro_lo, macro_hi; // depth of the shared "Animate" layer-gain contour
  float drift_depth;        // scales the per-layer independent gain drifts (1 = full)
  float lufs_bias;          // servo target offset: the servo is ungated, BS.1770 I is not
  float root_midi;          // key root
  const int *scale;         // pentatonic degrees, semitones
  int   scale_n;

  float pad_warm, pad_glass; // PADsynth profile mix (linear gains)
  float pad_cut;             // voice low-pass cutoff as a multiple of f0
  float pad_db;              // pad bus level, dB (reference 0 = full)
  float bass_db;

  float chord_min, chord_max;   // seconds between chord changes

  // Melody: phrases of `phrase_lo..phrase_hi` notes spaced `gap_lo..gap_hi`
  // seconds apart, then a rest of `rest_lo..rest_hi` seconds. Rates are per
  // PLAN v3 §2.3; intensity scales every interval by 0.4..1.6.
  minst_t inst[MODE_INST];
  float gap_lo, gap_hi;
  int   phrase_lo, phrase_hi;
  float rest_lo, rest_hi;
  int   vel_lo, vel_hi;         // MIDI velocity
  float dyad_p;                 // probability a trigger becomes a two-note dyad
  // Makeup gain for the FluidSynth bus. It is a large positive number because
  // the instruments are played at MIDI velocities of 28-62 through a forced
  // 170-300 ms attack and a per-channel attenuation that matches the four
  // presets to each other, which together leave the synth's output about 50 dB
  // below the pad bus. -99 = this mode has no melody.
  float melody_db;

  float pulse_bpm;              // 0 = no pulse
  float kick_db, tick_db;

  float plate_decay, plate_damp, plate_db;

  float noise_color;            // 0 = brown, 1 = pink
  float noise_db, noise_lp;     // noise_db <= -90 = no noise bed
  float nature_boost;           // dB the nature bed gains at tonal = 0
  float ocean_db;               // -99 = off (Environment drives it from `env` instead)
  // dB the loudness target drops at tonal = 0, where only the bed is left.
  // Environment keeps it at 0: its bed is the mode, not a fallback.
  float tonal_tilt;

  int   am_allowed;             // the 16 Hz AM toggle does anything in this mode
  float binaural_hz;
} omode_t;

extern const omode_t MODES[M_COUNT];

const char *mode_name(int m);
int   mode_from_name(const char *s);

const chord_t *mode_chord(int mode, int idx);
int   chord_next(int mode, int cur, rng_t *rng);

// Pick a chord-tone frequency for a pad voice: nearest to `prev_hz` (0 = free),
// pulled toward `center_hz`, inside [lo,hi], avoiding minor 2nds and tritones
// against any of the `navoid` frequencies in `avoid` that lie within 14 st.
float chord_pick(const chord_t *c, float root_hz, float prev_hz, float center_hz,
                 float lo, float hi, const float *avoid, int navoid, rng_t *rng);

// Snap a frequency to the nearest just interval above `root_hz`. See brain.c.
float ji_snap(float hz, float root_hz);

// Bass note: root or fifth of the chord, forced into 55..110 Hz.
float chord_bass_hz(const chord_t *c, float root_hz, rng_t *rng);

// Markov melody over pentatonic degrees. `deg` is an unbounded scale index
// (degree + 5*octave); the caller keeps it between calls.
int   melody_next_deg(int mode, int deg, int *repeat_run, rng_t *rng);
float degree_hz(int mode, int deg, float root_hz);

#endif
