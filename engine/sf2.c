// Omanoise v3 — libfluidsynth wrapper.
#include "sf2.h"

#include <stdio.h>
#include <fluidsynth.h>
#include <time.h>

// FluidR3_GM bank 0 programs. Acoustic keyboard / harp / mallet only: no
// synth leads, no brass, and explicitly no Tubular Bells (14), whose partials
// are inharmonic — the exact thing v3 removes.
//
// Kalimba (108) was the plan's third instrument and did not survive the audit:
// its samples decay in 80-200 ms, so the note peaks long before a forced
// 300-450 ms attack has finished and the measured onset-to-90 %-of-peak time
// swings between 82 and 413 ms across the register — under the 150 ms floor
// for several keys. Vibraphone (11), also on the plan's candidate list, is a
// flat 287 ms at every key from 48 to 96 and carries no motor tremolo.
typedef struct {
  int   prog;
  const char *name;
  float attack_s;     // target volume-envelope attack (see the note below)
  float dark_cents;   // fixed per-channel filter darkening
  float hold_s;       // note-on to note-off
  float trim_db;      // <= 0, per-channel level match (GEN_ATTENUATION offset)
} sf2_chan_t;

// `attack_s` is turned into a *channel generator offset* in timecents:
//   offset = 1200*log2(attack_s) + 12000
// SoundFont generators are additive, so this only lands on `attack_s` when the
// preset's own attackVolEnv is the SF2 default of -12000 timecents. All four
// presets below are; the `--render-bank` audit measures the result and is the
// authority (docs/ACCEPTANCE.md v3 §3).
static const sf2_chan_t CHANS[SF2_NCH] = {
  [SF2_PIANO]  = { 0,  "AcousticGrand",  0.260f, -500.0f, 6.0f,   0.0f },
  [SF2_HARP]   = { 46, "OrchestralHarp", 0.260f, -500.0f, 4.0f, -11.0f },
  [SF2_MALLET] = { 11, "Vibraphone",     0.300f, -400.0f, 5.0f,  -7.0f },
  [SF2_BOX]    = { 10, "MusicBox",       0.300f, -400.0f, 3.5f,   0.0f },
};

struct sf2 {
  fluid_settings_t *set;
  fluid_synth_t *syn;
  int sfid;
  float dark_cur;
  float pan_cur[SF2_NCH];
};

const char *sf2_channel_name(int ch)    { return (ch >= 0 && ch < SF2_NCH) ? CHANS[ch].name : "?"; }
int         sf2_channel_program(int ch) { return (ch >= 0 && ch < SF2_NCH) ? CHANS[ch].prog : -1; }
float       sf2_channel_hold(int ch)    { return (ch >= 0 && ch < SF2_NCH) ? CHANS[ch].hold_s : 3.0f; }

const char *sf2_path(void) {
  const char *e = getenv("OMANOISE_SF2");
  return (e && *e) ? e : "/usr/share/soundfonts/FluidR3_GM.sf2";
}

static void sf2_nolog(int level, const char *message, void *data) {
  (void)level; (void)message; (void)data;    // the RT thread must never print
}

static double now_ms_(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

sf2_t *sf2_create(const char *path, double *out_ms) {
  double t0 = now_ms_();
  if (out_ms) *out_ms = 0.0;
  FILE *probe = fopen(path, "rb");
  if (!probe) return NULL;
  fclose(probe);

  // FluidSynth's logging is global and its default handler writes to stderr;
  // silence it so nothing can print from the realtime thread later.
  for (int i = 0; i < LAST_LOG_LEVEL; i++) fluid_set_log_function(i, sf2_nolog, NULL);
  // Registering an empty driver list stops new_fluid_settings() from probing
  // ALSA/OSS/PulseAudio: that probe writes half a screen of ALSA errors
  // straight to stderr (bypassing the log handler) and costs ~8 MB of RSS.
  static const char *no_drivers[] = { NULL };
  fluid_audio_driver_register(no_drivers);

  sf2_t *s = calloc(1, sizeof(*s));
  if (!s) return NULL;
  s->set = new_fluid_settings();
  if (!s->set) { free(s); return NULL; }
  fluid_settings_setnum(s->set, "synth.sample-rate", (double)RATE);
  fluid_settings_setint(s->set, "synth.reverb.active", 0);      // we have a plate
  fluid_settings_setint(s->set, "synth.chorus.active", 0);      // rule 1: no LFOs
  fluid_settings_setint(s->set, "synth.polyphony", 48);
  fluid_settings_setint(s->set, "synth.midi-channels", 16);
  fluid_settings_setint(s->set, "synth.threadsafe-api", 0);     // RT thread owns it
  fluid_settings_setint(s->set, "synth.dynamic-sample-loading", 1);
  fluid_settings_setnum(s->set, "synth.gain", 0.4);

  s->syn = new_fluid_synth(s->set);
  if (!s->syn) { delete_fluid_settings(s->set); free(s); return NULL; }

  s->sfid = fluid_synth_sfload(s->syn, path, 1);
  if (s->sfid == FLUID_FAILED) {
    delete_fluid_synth(s->syn);
    delete_fluid_settings(s->set);
    free(s);
    return NULL;
  }

  for (int ch = 0; ch < SF2_NCH; ch++) {
    // This is the call that pages the sample data in; it must not happen on
    // the realtime thread, and with dynamic loading it never does again,
    // because the preset stays selected for the life of the process.
    fluid_synth_program_select(s->syn, ch, s->sfid, 0, CHANS[ch].prog);
    fluid_synth_set_gen(s->syn, ch, GEN_VOLENVATTACK,
                        1200.0f * log2f(CHANS[ch].attack_s) + 12000.0f);
    fluid_synth_set_gen(s->syn, ch, GEN_VOLENVRELEASE, 1200.0f);   // 2x, never abrupt
    fluid_synth_set_gen(s->syn, ch, GEN_FILTERFC, CHANS[ch].dark_cents);
    // Centibels of attenuation; only ever positive here, because a SoundFont
    // voice's total attenuation is clamped at zero and cannot be pushed into
    // gain. The four instruments are matched down to the quietest of them and
    // the mode's melody bus makes the level back up.
    fluid_synth_set_gen(s->syn, ch, GEN_ATTENUATION, -10.0f * CHANS[ch].trim_db);
    fluid_synth_cc(s->syn, ch, 10, 64);      // pan centre
    fluid_synth_cc(s->syn, ch, 91, 0);       // reverb send off
    fluid_synth_cc(s->syn, ch, 93, 0);       // chorus send off
    s->pan_cur[ch] = 0.0f;
  }
  s->dark_cur = 0.0f;

  // Pre-roll a little audio so every lazily built table inside the synth
  // exists before the realtime thread ever calls it.
  float l[64], r[64];
  for (int i = 0; i < 8; i++) fluid_synth_write_float(s->syn, 64, l, 0, 1, r, 0, 1);

  if (out_ms) *out_ms = now_ms_() - t0;
  return s;
}

void sf2_destroy(sf2_t *s) {
  if (!s) return;
  if (s->syn) delete_fluid_synth(s->syn);
  if (s->set) delete_fluid_settings(s->set);
  free(s);
}

void sf2_note_on(sf2_t *s, int ch, int key, int vel) {
  if (!s || ch < 0 || ch >= SF2_NCH) return;
  fluid_synth_noteon(s->syn, ch, key, vel);
}

void sf2_note_off(sf2_t *s, int ch, int key) {
  if (!s || ch < 0 || ch >= SF2_NCH) return;
  fluid_synth_noteoff(s->syn, ch, key);
}

void sf2_pan(sf2_t *s, int ch, float pan) {
  if (!s || ch < 0 || ch >= SF2_NCH) return;
  // CC10 is coarse (128 steps); only send when the position really moved, so a
  // slow drift cannot turn into a stream of stepped jumps.
  if (fabsf(pan - s->pan_cur[ch]) < 0.02f) return;
  s->pan_cur[ch] = pan;
  int v = (int)(64.0f + 63.0f * clampf(pan, -1.0f, 1.0f));
  fluid_synth_cc(s->syn, ch, 10, v < 0 ? 0 : (v > 127 ? 127 : v));
}

void sf2_set_dark(sf2_t *s, float cents) {
  if (!s) return;
  if (fabsf(cents - s->dark_cur) < 25.0f) return;
  s->dark_cur = cents;
  for (int ch = 0; ch < SF2_NCH; ch++)
    fluid_synth_set_gen(s->syn, ch, GEN_FILTERFC, CHANS[ch].dark_cents + cents);
}

void sf2_all_off(sf2_t *s) {
  if (!s) return;
  for (int ch = 0; ch < SF2_NCH; ch++) fluid_synth_all_notes_off(s->syn, ch);
}

void sf2_render(sf2_t *s, float *l, float *r, int frames) {
  if (!s) { memset(l, 0, (size_t)frames * sizeof(float)); memset(r, 0, (size_t)frames * sizeof(float)); return; }
  fluid_synth_write_float(s->syn, frames, l, 0, 1, r, 0, 1);
}

void sf2_audit_note(sf2_t *s, int ch, int key, int vel, float *out, int frames) {
  memset(out, 0, (size_t)frames * sizeof(float));
  if (!s) return;
  float l[64], r[64];
  fluid_synth_all_sounds_off(s->syn, ch);
  for (int i = 0; i < 64; i++) fluid_synth_write_float(s->syn, 64, l, 0, 1, r, 0, 1);
  fluid_synth_noteon(s->syn, ch, key, vel);
  for (int i = 0; i < frames; i += 64) {
    int k = frames - i < 64 ? frames - i : 64;
    fluid_synth_write_float(s->syn, k, l, 0, 1, r, 0, 1);
    for (int j = 0; j < k; j++) out[i + j] = 0.5f * (l[j] + r[j]);
  }
  fluid_synth_noteoff(s->syn, ch, key);
  for (int i = 0; i < 64; i++) fluid_synth_write_float(s->syn, 64, l, 0, 1, r, 0, 1);
}

int sf2_active_voices(sf2_t *s) {
  return s ? fluid_synth_get_active_voice_count(s->syn) : 0;
}
