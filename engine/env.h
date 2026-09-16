// Omanoise v5 — the Environment bank: six blendable sounds.
//
//   ocean   synthesized (the v3 wave engine, in layers.c)
//   rain    synthesized (v4.1, below)
//   fire    recorded    sounds/fireplace.wav
//   wind    recorded    sounds/wind.wav
//   stream  recorded    sounds/stream.wav
//   birds   recorded    sounds/birds.wav
//
// v4's synthesized fire and wind generators are gone. Two attempts at a
// convincing fire failed (user verdict: "like being on the phone with someone
// with bad internet in wind and rain"), and a public-domain recording is simply
// the right tool for a fireplace; wind followed it for the same reason.
//
// The surviving synthesized generator (rain) obeys the v3 psychoacoustic rules:
// the only slow movement is value noise well under 0.2 Hz and every event train
// is Poisson. The recorded player obeys them too — it never loops, never
// changes pitch, and its only modulator is a <= 0.05 Hz value-noise level drift.
#ifndef OMANOISE_ENV_H
#define OMANOISE_ENV_H

#include "common.h"
#include "dsp.h"
#include "wav.h"

enum { ENV_OCEAN = 0, ENV_RAIN, ENV_FIRE, ENV_WIND, ENV_STREAM, ENV_BIRDS, ENV_N };

extern const char *const ENV_NAME[ENV_N];
// The basename of the recording that drives each slot: ocean, rain, fireplace,
// wind, stream, birds. (Only `fire` differs from the slot name.)
extern const char *const ENV_FILE[ENV_N];
// -1 if the name is not one of the six.
int env_from_name(const char *s);

// ------------------------------------------------------------------ rain

#define ENV_DROPS 6

typedef struct {
  int   on;
  int   t, len, atk;          // samples
  float ph, f0, f1;           // chirp, turns and Hz
  float gL, gR;
} drop_t;

typedef struct {
  rng_t   rng[2];
  svf_t   bp[2], lp[2];       // patter: 2-pole band-pass then LP 3 kHz
  svf_t   hhp[2], hlp[2], hlp2[2];  // hiss: HP 1.8 kHz then LP 6.5 kHz (4-pole)
  svf_t   dhp[2];             // droplet HP 1 kHz
  drift_t centre_dr;          // band-pass centre, <= 0.05 Hz
  drift_t shower_dr;          // hiss level, <= 0.08 Hz
  drift_t pan_dr;             // droplet pan
  float   p_hit, hit_amp, hiss_g, drop_g, p_drop, pan;
  drop_t  drop[ENV_DROPS];
} rain_t;

// -------------------------------------------------- recorded-sound player
//
// Two read heads. Each plays a randomly chosen segment of the file at speed
// 1.0; when the leading head reaches its last crossfade-length, the other head
// starts a new random segment and the two cross-fade equal-power (sin/cos), so
// the sum of their powers is constant and there is no click and no seam. A
// plain loop of a 25 s fire would be recognisably periodic; this never repeats.

#define REC_LOG 64            // segments kept for the `--stats` segment log

typedef struct {
  int     on;
  int64_t start, len, t;      // frames: position in the file, length, played
} rhead_t;

typedef struct {
  const wav_t *w;             // published by the loader; NULL = slot unavailable
  rhead_t head[2];
  int     cur;                // the head that is not yet fading out
  rng_t   rng;
  svf_t   hp[2], lp[2];       // HP 40 Hz, LP 9 kHz +- 0.7 oct with Brightness
  drift_t lev_dr;             // +- 1.5 dB, <= 0.05 Hz
  float   lev_g;
  int64_t xf, seg_lo, seg_hi; // crossfade and segment-length bounds, frames
  int64_t span_lo, span_hi;   // legal segment window (1 s clear of both ends)
  float   log_start[REC_LOG], log_len[REC_LOG];   // seconds
  int     log_n;              // total segments started (the array holds the first REC_LOG)
} rec_t;

typedef struct { rain_t rain; rec_t rec[ENV_N]; } envbank_t;
// The loaded recordings, one optional file per slot. Published to the realtime
// thread with a single atomic pointer swap.
typedef struct { wav_t w[ENV_N]; } recbank_t;

void env_init(envbank_t *e, uint64_t seed);
// Control rate (once per BLOCK). `lev` is the six user levels 0..1; only the
// rain event rate uses them (and Intensity), the gains are applied by the
// caller.
void env_control(envbank_t *e, float dt, float bright, float intensity, const float *lev);

// Audio rate. Each writes one stereo sample pair at unit level; the caller
// applies the per-sound gain. Never called for a sound whose level is zero.
void env_rain(rain_t *r, float *outL, float *outR);

// ---- recorded player. rec_attach() is control rate (it only ever runs when
// the published pointer changes); rec_audio() is realtime and allocation-free.
void  rec_attach(rec_t *r, const wav_t *w);
void  rec_control(rec_t *r, float dt, float bright);
void  rec_audio(rec_t *r, float *outL, float *outR);
static inline int rec_ready(const rec_t *r) { return r->w != NULL; }

#endif
