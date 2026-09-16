// Omanoise v3 — sampled instrument layer (libfluidsynth + FluidR3_GM.sf2).
//
// The v2 melody was hand-synthesized: Risset and FM bells with inharmonic
// partials and 12-35 ms attacks. Both are startle/roughness sources
// (research-opensource, "sharp transients" and "roughness"), and the user
// named that layer as the anxious one. v3 plays recorded acoustic instruments
// instead, with their attacks forced open to >= 150 ms.
//
// Threading contract, because `synth.threadsafe-api` is 0:
//   * sf2_create() runs on the bank worker thread and does everything that can
//     allocate or touch the disk — settings, synth, sfload, program_select
//     (which is what loads sample data when dynamic-sample-loading is on) and
//     the per-channel generators.
//   * Only after it returns may the pointer be published to the realtime
//     thread, and from then on *every* call must come from that thread.
// Measured on FluidSynth 2.6.0: program_select is where the sample data is
// paged in (RSS grows there, 0.1-4 ms per preset); fluid_synth_noteon after
// that costs 4-20 us and does not grow RSS, i.e. it never loads from disk.
#ifndef OMANOISE_SF2_H
#define OMANOISE_SF2_H

#include "common.h"

// Channel slots. Index into the table in sf2.c; also the MIDI channel.
enum { SF2_PIANO = 0, SF2_HARP, SF2_MALLET, SF2_BOX, SF2_NCH };

typedef struct sf2 sf2_t;

// Path of the SoundFont, honouring $OMANOISE_SF2.
const char *sf2_path(void);

// Worker thread only. Returns NULL if the SoundFont is missing or unreadable;
// `*out_ms` gets the load time either way. Never prints.
sf2_t *sf2_create(const char *path, double *out_ms);
void   sf2_destroy(sf2_t *s);

// Names/programs of the loaded channels, for logging and `--render-bank`.
const char *sf2_channel_name(int ch);
int         sf2_channel_program(int ch);
// How long a note of this instrument is held before note-off: long enough for
// the sample to have decayed on its own, so the release is never audible.
float       sf2_channel_hold(int ch);

// ---- realtime thread only ----
void sf2_note_on(sf2_t *s, int ch, int key, int vel);
void sf2_note_off(sf2_t *s, int ch, int key);
void sf2_pan(sf2_t *s, int ch, float pan);        // -1..1, smoothed by CC
// Extra darkening in cents, relative to each channel's built-in cutoff. 0 is
// the preset's own value; negative is darker. Only re-sent when it moves.
void sf2_set_dark(sf2_t *s, float cents);
void sf2_all_off(sf2_t *s);
// Writes exactly `frames` stereo frames (overwrites, does not add).
void sf2_render(sf2_t *s, float *l, float *r, int frames);
int  sf2_active_voices(sf2_t *s);

// Offline audit only (`--render-bank`): plays one note into a mono buffer from
// silence, so the attack, peak and spectral centroid of what the engine will
// actually trigger can be measured. Not realtime-safe.
void sf2_audit_note(sf2_t *s, int ch, int key, int vel, float *out, int frames);

#endif
